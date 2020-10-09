// Copyright (c) 2020 The Zcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ecdsa_context.h"
#include "lelantuswallet.h"
#include "walletmodels.h"

#include "../uint256.h"

#include "../crypto/hmac_sha256.h"
#include "../crypto/hmac_sha512.h"

#include "../wallet/wallet.h"
#include "../wallet/walletdb.h"
#include "../wallet/walletexcept.h"

#include <boost/optional.hpp>

#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace elysium {

LelantusWallet::MintPoolEntry::MintPoolEntry()
{
}

LelantusWallet::MintPoolEntry::MintPoolEntry(MintEntryId const &id, CKeyID const &seedId, uint32_t index)
    : id(id), seedId(seedId), index(index)
{
}

bool LelantusWallet::MintPoolEntry::operator==(MintPoolEntry const &another) const
{
    return id == another.id &&
        seedId == another.seedId &&
        index == another.index;
}

bool LelantusWallet::MintPoolEntry::operator!=(MintPoolEntry const &another) const
{
    return !(*this == another);
}

LelantusWallet::LelantusWallet(Database *database)
    : walletFile(pwalletMain->strWalletFile), database(database), context(ECDSAContext::CreateSignContext())
{
}

void LelantusWallet::ReloadMasterKey()
{
    LOCK(pwalletMain->cs_wallet);

    if (pwalletMain->IsLocked()) {
        throw std::runtime_error("Unable to reload master key because wallet is locked");
    }

    masterId = pwalletMain->GetHDChain().masterKeyID;

    if (masterId.IsNull()) {
        throw std::runtime_error("Master id is null");
    }

    // Load mint pool from DB
    LoadMintPool();

    // Clean up any mint entries that aren't corresponded to current masterId
    RemoveInvalidMintPoolEntries();

    // Refill mint pool
    FillMintPool();
}

uint32_t LelantusWallet::GenerateNewSeed(CKeyID &seedId, uint512 &seed)
{
    LOCK(pwalletMain->cs_wallet);
    seedId = pwalletMain->GenerateNewKey(BIP44ChangeIndex()).GetID();
    return GenerateSeed(seedId, seed);
}

uint32_t LelantusWallet::GenerateSeed(CKeyID const &seedId, uint512 &seed)
{
    LOCK(pwalletMain->cs_wallet);
    CKey key;
    if (!pwalletMain->GetKey(seedId, key)) {
        throw std::runtime_error(
            "Unable to retrieve generated key for mint seed. Is the wallet locked?");
    }

    // HMAC-SHA512(key, count)
    // `count` is LE unsigned 32 bits integer
    std::array<unsigned char, CSHA512::OUTPUT_SIZE> result;
    uint32_t change;
    auto seedIndex = GetSeedIndex(seedId, change);

    if (change != BIP44ChangeIndex()) {
        throw std::invalid_argument("BIP44 Change of seed id is invalid");
    }

    CHMAC_SHA512(key.begin(), key.size()).
        Write(reinterpret_cast<const unsigned char*>(&seedIndex), sizeof(seedIndex)).
        Finalize(result.data());

    seed = uint512(result);

    return seedIndex;
}

namespace {

uint32_t GetBIP44AddressIndex(std::string const &path, uint32_t &change)
{
    uint32_t index;
    if (sscanf(path.data(), "m/44'/%*u'/%*u'/%u/%u", &change, &index) != 2) {
        throw std::runtime_error("Fail to match BIP44 path");
    }

    return index;
}

}

uint32_t LelantusWallet::GetSeedIndex(CKeyID const &seedId, uint32_t &change)
{
    LOCK(pwalletMain->cs_wallet);
    auto it = pwalletMain->mapKeyMetadata.find(seedId);
    if (it == pwalletMain->mapKeyMetadata.end()) {
        throw std::runtime_error("key not found");
    }

    // parse last index
    try {
        return GetBIP44AddressIndex(it->second.hdKeypath, change);
    } catch (std::runtime_error const &e) {
        LogPrintf("%s : fail to get child from, %s\n", __func__, e.what());
        throw;
    }
}

bool LelantusWallet::GetPublicKey(ECDSAPrivateKey const &priv, secp256k1_pubkey &out)
{
    return 1 == secp256k1_ec_pubkey_create(
        context.Get(),
        &out,
        priv.data());
}

secp_primitives::Scalar LelantusWallet::GenerateSerial(secp256k1_pubkey const &pubkey)
{
    std::array<uint8_t, 33> compressedPub;

    size_t outSize = compressedPub.size();
    secp256k1_ec_pubkey_serialize(
        context.Get(),
        compressedPub.data(),
        &outSize,
        &pubkey,
        SECP256K1_EC_COMPRESSED);

    if (outSize != 33) {
        throw std::runtime_error("Compressed public key size is invalid.");
    }

    std::array<uint8_t, CSHA256::OUTPUT_SIZE> hash;
    CSHA256().Write(compressedPub.data(), compressedPub.size()).Finalize(hash.data());

    secp_primitives::Scalar serial;
    serial.memberFromSeed(hash.data());

    return serial;
}

uint32_t LelantusWallet::BIP44ChangeIndex() const
{
    return BIP44_ELYSIUM_LELANTUSMINT_INDEX;
}

LelantusPrivateKey LelantusWallet::GeneratePrivateKey(uint512 const &seed)
{
    auto params = lelantus::Params::get_default();

    ECDSAPrivateKey signatureKey;

    // last 32 bytes as seed of randomness
    std::array<uint8_t, 32> randomnessSeed;
    std::copy(seed.begin() + 32, seed.end(), randomnessSeed.begin());
    secp_primitives::Scalar randomness;
    randomness.memberFromSeed(randomnessSeed.data());

    // first 32 bytes as seed of ecdsa key and serial
    std::copy(seed.begin(), seed.begin() + 32, signatureKey.begin());

    // hash until get valid private key
    secp256k1_pubkey pubkey;
    do {
        CSHA256().Write(signatureKey.data(), signatureKey.size()).Finalize(signatureKey.data());
    } while (!GetPublicKey(signatureKey, pubkey));

    auto serial = GenerateSerial(pubkey);

    return LelantusPrivateKey(params, serial, randomness, signatureKey);
}

// Mint Updating
void LelantusWallet::WriteMint(LelantusMintId const &id, LelantusMint const &mint)
{
    if (!database->WriteMint(id, mint)) {
        throw std::runtime_error("fail to write hdmint");
    }

    if (!database->WriteMintId(mint.serialId, id)) {
        throw std::runtime_error("fail to record id");
    }

    RemoveFromMintPool(id.id);
    FillMintPool();
}

LelantusPrivateKey LelantusWallet::GeneratePrivateKey(CKeyID const &seedId)
{
    uint512 seed;

    GenerateSeed(seedId, seed);
    return GeneratePrivateKey(seed);
}

LelantusMintId LelantusWallet::GenerateMint(PropertyId property, LelantusAmount amount, boost::optional<CKeyID> seedId)
{
    LOCK(pwalletMain->cs_wallet);

    // If not specify seed to use that mean caller want to generate a new mint.
    if (!seedId) {
        if (pwalletMain->IsLocked()) {
            throw WalletLocked();
        }

        if (mintPool.empty()) {

            // Try to recover mint pools
            ReloadMasterKey();

            if (mintPool.empty()) {
                throw std::runtime_error("Mint pool is empty");
            }
        }

        seedId = mintPool.begin()->seedId;
    }

    // Generate private & public key.
    auto priv = GeneratePrivateKey(seedId.get());
    auto mintId = MintEntryId(priv.serial, priv.randomness, seedId.get());

    // Create a new mint.
    auto serialId = GetSerialId(priv.serial);
    LelantusMint mint(property, amount, seedId.get(), serialId);
    LelantusMintId id(property, amount, mintId);

    WriteMint(id, mint);

    return id;
}

LelantusMint LelantusWallet::UpdateMint(LelantusMintId const &id, std::function<void(LelantusMint &)> const &modifier)
{
    auto m = GetMint(id);
    modifier(m);

    if (!database->WriteMint(id, m)) {
        throw std::runtime_error("fail to update mint");
    }

    return m;
}

void LelantusWallet::ClearMintsChainState()
{
    CWalletDB db(walletFile);
    std::vector<std::pair<LelantusMintId, LelantusMint>> mints;

    db.TxnBegin();

    ListMints(std::back_inserter(mints), &db);

    for (auto &m : mints) {
        m.second.chainState = LelantusMintChainState();
        m.second.spendTx = uint256();

        if (!database->WriteMint(m.first, m.second, &db)) {
            throw std::runtime_error("Failed to write " + walletFile);
        }
    }

    db.TxnCommit();
}

bool LelantusWallet::TryRecoverMint(
    LelantusMintId const &id,
    LelantusMintChainState const &chainState,
    uint256 const &spendTx)
{
    // LOCK(pwalletMain->cs_wallet);

    // MintPoolEntry entry;
    // if (!GetMintPoolEntry(id, entry)) {
    //     return false;
    // }

    // // Regenerate the mint
    // auto const &seedId = entry.seedId;

    // uint512 seed;
    // GenerateSeed(seedId, seed);

    // auto privKey = GeneratePrivateKey(seed);

    // auto serialId = GetSerialId(privKey.getSerialNumber());

    // // Create mint object
    // SigmaMint mint(
    //     id.property,
    //     id.denomination,
    //     seedId,
    //     serialId);
    // mint.chainState = chainState;
    // mint.spendTx = spendTx;

    // WriteMint(id, mint);

    // return true;
}

bool LelantusWallet::TryRecoverMint(
    LelantusMintId const &id,
    LelantusMintChainState const &chainState)
{
    return TryRecoverMint(id, chainState, uint256());
}

void LelantusWallet::UpdateMintCreatedTx(LelantusMintId const &id, const uint256& tx)
{
    UpdateMint(id, [&](LelantusMint& m) {
        m.createdTx = tx;
    });
}

void LelantusWallet::UpdateMintChainstate(LelantusMintId const &id, LelantusMintChainState const &state)
{
    UpdateMint(id, [&](LelantusMint &m) {
        m.chainState = state;
    });
}

void LelantusWallet::UpdateMintSpendTx(LelantusMintId const &id, uint256 const &tx)
{
    UpdateMint(id, [&](LelantusMint &m) {
        m.spendTx = tx;
    });
}

// Mint querying
bool LelantusWallet::HasMint(LelantusMintId const &id) const
{
    return database->HasMint(id);
}

bool LelantusWallet::HasMint(secp_primitives::Scalar const &serial) const
{
    auto id = GetSerialId(serial);
    return database->HasMintId(id);
}

LelantusMint LelantusWallet::GetMint(LelantusMintId const &id) const
{
    LelantusMint m;
    if (!database->ReadMint(id, m)) {
        throw std::runtime_error("fail to read hdmint");
    }

    return m;
}

LelantusMint LelantusWallet::GetMint(secp_primitives::Scalar const &serial) const
{
    return GetMint(GetMintId(serial));
}

LelantusMintId LelantusWallet::GetMintId(secp_primitives::Scalar const &serial) const
{
    LelantusMintId id;
    auto serialHash = GetSerialId(serial);
    if (!database->ReadMintId(serialHash, id)) {
        throw std::runtime_error("fail to read id");
    }

    return id;
}

// MintPool state
void LelantusWallet::RemoveInvalidMintPoolEntries() // Remove MintPool entry that isn't belong to current masterId.
{
    LOCK(pwalletMain->cs_wallet);

    bool updated = false;
    for (auto it = mintPool.begin(); it != mintPool.end(); it++) {

        auto metaIt = pwalletMain->mapKeyMetadata.find(it->seedId);
        if (metaIt == pwalletMain->mapKeyMetadata.end() ||
            metaIt->second.hdMasterKeyID != masterId) {

            updated = true;
            mintPool.erase(it);
        }
    }

    if (updated) {
        SaveMintPool();
    }
}

void LelantusWallet::DeleteUnconfirmedMint(LelantusMintId const &id)
{
    // LelantusMint mint;
    // if (!database->ReadMint(id, mint)) {
    //     throw std::runtime_error("no mint data in wallet");
    // }

    // if (mint.IsOnChain()) {
    //     throw std::invalid_argument("try to delete onchain mint");
    // }

    // auto pubKey = GeneratePrivateKey(mint.seedId).getPublicCoin();

    // uint32_t change;
    // auto index = GetSeedIndex(mint.seedId, change);

    // if (change != BIP44ChangeIndex()) {
    //     throw std::invalid_argument("Try to delete invalid seed id mint");
    // }

    // mintPool.insert(MintPoolEntry(pubKey, mint.seedId, index));
    // SaveMintPool();

    // if (!database->EraseMint(id)) {
    //     throw std::runtime_error("fail to erase mint from wallet");
    // }
}

bool LelantusWallet::IsMintInPool(LelantusMintId const &id)
{
    LOCK(pwalletMain->cs_wallet);
    return mintPool.get<1>().count(id.id);
}

bool LelantusWallet::GetMintPoolEntry(LelantusMintId const &id, MintPoolEntry &entry)
{
    LOCK(pwalletMain->cs_wallet);

    auto &publicKeyIndex = mintPool.get<1>();
    auto it = publicKeyIndex.find(id.id);

    if (it == publicKeyIndex.end()) {
        return false;
    }

    entry = *it;
    return true;
}

// Generate coins to mint pool until amount of coins in mint pool touch the expected amount.
size_t LelantusWallet::FillMintPool()
{
    LOCK(pwalletMain->cs_wallet);

    size_t generatedCoins = 0;
    while (mintPool.size() < MINTPOOL_CAPACITY) {

        CKeyID seedId;
        uint512 seed;

        auto index = GenerateNewSeed(seedId, seed);
        auto privKey = GeneratePrivateKey(seed);

        // privKey.getPublicCoin();
        // SigmaPublicKey pubKey(privKey, DefaultSigmaParams);
        // mintPool.insert(MintPoolEntry(pubKey, seedId, index));

        generatedCoins++;
    }

    if (generatedCoins)  {
        SaveMintPool();
    }

    return generatedCoins;
}

void LelantusWallet::LoadMintPool()
{
    LOCK(pwalletMain->cs_wallet);

    mintPool.clear();

    std::vector<MintPoolEntry> mintPoolData;
    if (database->ReadMintPool(mintPoolData)) {
        for (auto &entry : mintPoolData) {
            mintPool.insert(std::move(entry));
        }
    }

    LogPrintf("%s : load mint pool size %d\n", __func__, mintPool.size());
}

void LelantusWallet::SaveMintPool()
{
    LOCK(pwalletMain->cs_wallet);

    std::vector<MintPoolEntry> mintPoolData;
    for (auto const &entry : mintPool) {
        mintPoolData.push_back(entry);
    }

    if (!database->WriteMintPool(mintPoolData)) {
        throw std::runtime_error("fail to save mint pool to DB");
    }
}

bool LelantusWallet::RemoveFromMintPool(MintEntryId const &id)
{
    LOCK(pwalletMain->cs_wallet);

    auto &mintIdIndex = mintPool.get<1>();
    auto it = mintIdIndex.find(id);

    if (it != mintIdIndex.end()) {

        mintIdIndex.erase(it);
        SaveMintPool();
        return true;
    }

    // mint is not in the pool
    return false;
}

LelantusWallet::Database::Connection::Connection(CWalletDB *db)
{
    if (db) {
        this->db.db = db;
        local = false;
    } else {
        new (this->db.local) CWalletDB(pwalletMain->strWalletFile);
        local = true;
    }
}

LelantusWallet::Database::Connection::~Connection()
{
    if (local) {
        reinterpret_cast<CWalletDB*>(db.local)->~CWalletDB();
    }
}

CWalletDB* LelantusWallet::Database::Connection::operator->() noexcept
{
    return local ? reinterpret_cast<CWalletDB*>(db.local) : db.db;
}

LelantusWallet::Database::Database()
{
}

} // namespace elysium
