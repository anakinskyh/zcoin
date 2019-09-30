// Copyright (c) 2019 The Zcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCOIN_EXODUS_SIGMAWALLET_H
#define ZCOIN_EXODUS_SIGMAWALLET_H

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include "../uint256.h"
#include "../wallet/wallet.h"

#include "sigmadb.h"
#include "walletmodels.h"

namespace exodus {

struct MintPoolEntry {
    SigmaPublicKey key;
    CKeyID seedId;

    MintPoolEntry();
    MintPoolEntry(SigmaPublicKey const &key, CKeyID const &seedId);

    bool operator==(MintPoolEntry const &) const;
    bool operator!=(MintPoolEntry const &) const;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(key);
        READWRITE(seedId);
    }
};

class SigmaWallet
{
protected:

    typedef boost::multi_index_container<
        MintPoolEntry,
        boost::multi_index::indexed_by<
            // Sequence
            boost::multi_index::sequenced<>,
            // Public Key index
            boost::multi_index::hashed_unique<
                boost::multi_index::member<MintPoolEntry, SigmaPublicKey, &MintPoolEntry::key>,
                std::hash<SigmaPublicKey>
            >
        >
    > MintPool;

    std::string walletFile;
    MintPool mintPool;
    uint160 masterId;

    static constexpr unsigned mintPoolCapacity = 20;

public:
    SigmaWallet();

private:
    void ReloadMasterKey();

    // Generator
private:
    uint32_t GenerateNewSeed(CKeyID &seedId, uint512 &seed);
    uint32_t GenerateSeed(CKeyID const &seedId, uint512 &seed);
    uint32_t GetSeedIndex(CKeyID const &seedId);

protected:
    bool GeneratePrivateKey(uint512 const &seed, exodus::SigmaPrivateKey &coin);

    // Mint updating
public:
    SigmaPrivateKey GeneratePrivateKey(CKeyID const &seedId);
    std::pair<SigmaMint, SigmaPrivateKey> GenerateMint(
        PropertyId propertyId,
        SigmaDenomination denom,
        boost::optional<CKeyID> seedId = boost::none);

    void ClearMintsChainState();
    bool TryRecoverMint(
        SigmaMintId const &id,
        SigmaMintChainState const &chainState);

private:
    bool SetMintSeedSeen(
        MintPoolEntry const &mintPoolEntry,
        PropertyId propertyId,
        SigmaDenomination denomination,
        exodus::SigmaMintChainState const &chainState,
        uint256 const &spendTx = uint256());
    SigmaMint UpdateMint(SigmaMintId const &, std::function<void(SigmaMint &)> const &);
    void WriteMint(SigmaMintId const &id, SigmaMint const &entry);

public:
    SigmaMint UpdateMintChainstate(SigmaMintId const &id, SigmaMintChainState const &state);
    SigmaMint UpdateMintSpendTx(SigmaMintId const &id, uint256 const &tx);

    // Mint querying
public:

    bool HasMint(SigmaMintId const &id) const;
    bool HasMint(secp_primitives::Scalar const &serial) const;
    SigmaMint GetMint(SigmaMintId const &id) const;
    SigmaMint GetMint(secp_primitives::Scalar const &serial) const;
    SigmaMintId GetMintId(secp_primitives::Scalar const &serial) const;

    template<
        class OutIt,
        typename std::enable_if<is_iterator<OutIt>::value>::type* = nullptr
    > OutIt ListMints(OutIt it, bool unusedOnly, bool matureOnly) const
    {
        ListMints([&it, unusedOnly, matureOnly](SigmaMint const &m) {

            auto used = !m.spendTx.IsNull();

            if (unusedOnly && used) {
                return;
            }

            auto confirmed = m.chainState.block >= 0;
            if (matureOnly && !confirmed) {
                return;
            }

            *it++ = m;
        });

        return it;
    }
    size_t ListMints(std::function<void(SigmaMint const&)> const &) const;

    // MintPool state
public:
    bool IsMintInPool(SigmaPublicKey const &pubKey);
    bool GetMintPoolEntry(SigmaPublicKey const &pubKey, MintPoolEntry &entry);

protected:
    void RemoveInvalidMintPoolEntries(); // Remove MintPool entry that isn't belong to current masterId.
    size_t FillMintPool();
    void LoadMintPool();
    void SaveMintPool();
    bool RemoveFromMintPool(SigmaPublicKey const &publicKey);
};

} // namespace exodus

#endif // ZCOIN_EXODUS_SIGMAWALLET_H