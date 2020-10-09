// Copyright (c) 2020 The Zcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "../primitives/zerocoin.h"
#include "../liblelantus/lelantus_primitives.h"

#include "lelantusprimitives.h"

namespace elysium {

MintEntryId::MintEntryId()
{
    SetNull();
}

MintEntryId::MintEntryId(lelantus::PrivateCoin const &coin, uint160 const &seedId)
    : MintEntryId(coin.getSerialNumber(), coin.getRandomness(), seedId)
{
}

MintEntryId::MintEntryId(secp_primitives::Scalar const &serial, secp_primitives::Scalar const &randomness, uint160 const &seedId)
{
    auto params = lelantus::Params::get_default();
    auto pubcoin = lelantus::LelantusPrimitives::commit(
        params->get_g(), serial, params->get_h0(), randomness);

    auto hashPub = primitives::GetPubCoinValueHash(pubcoin);
    CDataStream ss(SER_GETHASH, CLIENT_VERSION);
    ss << hashPub;
    ss << seedId;

    *this = MintEntryId(Hash(ss.begin(), ss.end()));
}

MintEntryId::MintEntryId(uint256 const &tag) : uint256(tag)
{
}

LelantusPrivateKey::LelantusPrivateKey(
    lelantus::Params const *_params,
    secp_primitives::Scalar const &_serial,
    secp_primitives::Scalar const &_randomness,
    ECDSAPrivateKey const &_ecdsaPrivateKey
    ) : params(_params), serial(_serial), randomness(_randomness), ecdsaPrivateKey(_ecdsaPrivateKey)
{
}

lelantus::PrivateCoin LelantusPrivateKey::GetPrivateCoin(LelantusAmount amount) const
{
    return lelantus::PrivateCoin(
        params,
        serial,
        amount,
        randomness,
        std::vector<unsigned char>(ecdsaPrivateKey.begin(), ecdsaPrivateKey.end()),
        LELANTUS_TX_VERSION_4);
}

} // namespace elysium