#include "../coinsigner.h"

#include "../../test/test_bitcoin.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/test/unit_test.hpp>

namespace std {

template<typename Char, typename Traits, size_t Size>
basic_ostream<Char, Traits>& operator<<(basic_ostream<Char, Traits>& os, const array<uint8_t, Size>& arr)
{
    vector<basic_string<Char, Traits>> strings;

    for (auto& m : arr) {
        basic_stringstream<Char, Traits> s;
        s << m;
        strings.push_back(s.str());
    }

    return os << '[' << boost::algorithm::join(strings, ", ") << ']';
}

} // namespace std

namespace elysium {

class TestCoinSigner : public CoinSigner
{
public:
    TestCoinSigner(ECDSAPrivateKey const &priv) : CoinSigner(priv)
    {
    }

    std::array<uint8_t, 32> const &GetKey() const {
        return CoinSigner::key;
    }
};

BOOST_FIXTURE_TEST_SUITE(exodus_coinsigner_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(construct_withvalid_keysize)
{
    ECDSAPrivateKey key;
    std::fill(key.begin(), key.end(), 0xFF);

    std::unique_ptr<TestCoinSigner> signer;
    BOOST_CHECK_NO_THROW(signer.reset(new TestCoinSigner(key)));

    auto retrievedKey = signer->GetKey();
    BOOST_CHECK_EQUAL(key, retrievedKey);
}

BOOST_AUTO_TEST_CASE(getpublickey)
{
    ECDSAPrivateKey key;
    std::fill(key.begin(), key.end(), 0x11);

    TestCoinSigner signer(key);
    auto pubkey = signer.GetPublicKey();

    BOOST_CHECK_EQUAL(
        "034f355bdcb7cc0af728ef3cceb9615d90684bb5b2ca5f859ab0f0b704075871aa",
        HexStr(pubkey));
}

BOOST_AUTO_TEST_CASE(ecdsasign)
{
    ECDSAPrivateKey key;
    std::fill(key.begin(), key.end(), 0x11);

    TestCoinSigner signer(key);
    auto msg = ParseHex("6483023e2c7bdc9e719708f49d08f3b2c8da6f42347317543ac77bda6199f470");

    auto sig = signer.Sign(msg.data(), msg.data() + msg.size());

    BOOST_CHECK_EQUAL(
        "5b14bb77da666264fc571b6a3c7d2f7268be55abca0228d2c3f6daf0b7b554b11792d3203c8983f2db2e21dd93d070eaa7ebf31ffc71ef01bd5816cf42825254",
        HexStr(sig));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace elysium