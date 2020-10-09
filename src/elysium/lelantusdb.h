// Copyright (c) 2020 The Zcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCOIN_ELYSIUM_LELANTUSDB_H
#define ZCOIN_ELYSIUM_LELANTUSDB_H

#include "../dbwrapper.h"
#include "../sync.h"
#include "../liblelantus/coin.h"

#include "lelantusprimitives.h"
#include "persistence.h"
#include "property.h"

namespace elysium {

class LelantusDb : public CDBBase
{
protected:
    CCriticalSection cs;

    static const size_t DEFAULT_GROUPSIZE = 65000;
    static const size_t DEFAULT_STARTCOINS = 16000;

    size_t groupSize;
    size_t startGroupSize;

public:
    LelantusDb(const boost::filesystem::path& path, bool wipe, size_t groupSize = DEFAULT_GROUPSIZE, size_t startCoins = DEFAULT_STARTCOINS);

public:
    bool HasSerial(PropertyId id, Scalar const &serial, uint256 &spendTx);
    bool WriteSerial(PropertyId id, secp_primitives::Scalar serial, int block, uint256 const &spendTx);

    std::vector<lelantus::PublicCoin> GetAnonimityGroup(PropertyId id, int groupId, size_t count);
    bool HasMint(PropertyId propertyId, lelantus::PublicCoin const &pubKey);
    bool HasMint(PropertyId propertyId, MintEntryId const &id);
    bool WriteMint(PropertyId propertyId, lelantus::PublicCoin const &pubKey, int block, MintEntryId const &id, LelantusAmount amount, std::vector<unsigned char> const &additional);

    void DeleteAll(int startBlock);

    void CommitCoins();

public:
    boost::signals2::signal<void(PropertyId, MintEntryId, LelantusGroup, LelantusIndex, boost::optional<LelantusAmount>)> MintAdded;
    boost::signals2::signal<void(PropertyId, MintEntryId)> MintRemoved;

protected:

    template<typename ...T>
    uint64_t GetNextSequence(T ...t) {
        auto key = std::make_tuple(t..., UINT64_MAX);
        auto it = NewIterator();

        {
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << key;
            it->Seek(ss.str());
        }

        if (!it->Valid()) {
            return 0;
        }

        it->Prev();
        if (!it->Valid()) {
            return 0;
        }

        std::tuple<T..., uint64_t> key2;
        {
            auto k = it->key();
            CDataStream ss(k.data(), k.data() + k.size(), SER_DISK, CLIENT_VERSION);
            ss >> key2;
        }

        std::get<sizeof...(t)>(key) = std::get<sizeof...(t)>(key2);

        if (key != key2) {
            return 0;
        }

        auto v = std::get<sizeof...(t)>(key);
        swapByteOrder(v);

        return v + 1;
    }

    bool WriteGroupSize(size_t groupSize, size_t mintAmount);
    std::pair<size_t, size_t> ReadGroupSize();

    int GetLastGroup(PropertyId id, size_t &coins);

    std::unique_ptr<leveldb::Iterator> NewIterator();
};

extern LelantusDb *lelantusDb;

} // namespace elysium

#endif // ZCOIN_ELYSIUM_LELANTUSDB_H