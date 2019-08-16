#include "../sigma.h"
#include "../../test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(exodus_sigma_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(private_key)
{
    exodus::SigmaPrivateKey key;

    auto serial = key.GetSerial();
    auto randomness = key.GetRandomness();

    key.Generate();

    BOOST_CHECK(key.IsValid());
    BOOST_CHECK_NE(key.GetSerial(), serial);
    BOOST_CHECK_NE(key.GetRandomness(), randomness);
}

BOOST_AUTO_TEST_CASE(public_key)
{
    exodus::SigmaPrivateKey priv;
    exodus::SigmaPublicKey pub;

    auto commit = pub.GetCommitment();

    priv.Generate();
    pub.Generate(priv);

    BOOST_CHECK(pub.IsValid());
    BOOST_CHECK_NE(pub.GetCommitment(), commit);

    // Try a second time to see if we still get the same result.
    commit = pub.GetCommitment();
    pub.Generate(priv);

    BOOST_CHECK_EQUAL(pub.GetCommitment(), commit);
}

BOOST_AUTO_TEST_CASE(proof)
{
    // Create keys.
    exodus::SigmaPrivateKey key1, key2, key3;

    key1.Generate();
    key2.Generate();
    key3.Generate();

    // Crete proof.
    exodus::SigmaProof proof;
    std::vector<exodus::SigmaPublicKey> pubs({
        exodus::SigmaPublicKey(key1),
        exodus::SigmaPublicKey(key2),
        exodus::SigmaPublicKey(key3)
    });

    proof.Generate(key2, pubs.begin(), pubs.end());

    BOOST_CHECK_EQUAL(proof.Verify(sigma::Params::get_default(), pubs.begin(), pubs.end()), true);
    BOOST_CHECK_EQUAL(proof.Verify(sigma::Params::get_default(), pubs.begin(), pubs.end() - 1), false);
}

#define SIGMA_MAX_GROUP_SIZE 16384

BOOST_AUTO_TEST_CASE(fullgroup)
{
    std::vector<exodus::SigmaPublicKey> pubs;
    pubs.reserve(SIGMA_MAX_GROUP_SIZE + 1);
    exodus::SigmaPrivateKey priv;
    while (pubs.size() < SIGMA_MAX_GROUP_SIZE + 1) {
        priv.Generate();
        pubs.push_back(exodus::SigmaPublicKey(priv));
    }

    exodus::SigmaProof proof;
    proof.Generate(priv, pubs.begin() + 1, pubs.end()); // use SIGMA_MAX_GROUP_SIZE coins
    BOOST_CHECK(proof.Verify(sigma::Params::get_default(), pubs.begin() + 1, pubs.end()));

    // exceed group size
    exodus::SigmaProof invalidProof;
    invalidProof.Generate(priv, pubs.begin(), pubs.end()); // use SIGMA_MAX_GROUP_SIZE + 1 coins
    BOOST_CHECK(!invalidProof.Verify(sigma::Params::get_default(), pubs.begin(), pubs.end()));
}

BOOST_AUTO_TEST_SUITE_END()
