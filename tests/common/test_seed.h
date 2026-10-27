// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The seed of a test's own randomness. A run uses the test's default seed,
// or the one it is given: --seed N on the command line or TRAVERSO_TEST_SEED=N
// in the environment (ctest passes the environment on). The seed is printed
// with every run, so a failing run can be repeated with it.
//
// A test with several generators asks a seed for each by its old fixed seed:
// with the default seed it gets exactly that one (the test runs as it always
// did), with another seed one made from both.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class TestSeed
{
public:
    TestSeed(int argc, char** argv, std::uint64_t defaultSeed)
        : m_default(defaultSeed)
        , m_seed(defaultSeed)
    {
        if (const char* environment = std::getenv("TRAVERSO_TEST_SEED")) {
            m_seed = std::strtoull(environment, nullptr, 10);
        }
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--seed") == 0) {
                m_seed = std::strtoull(argv[i + 1], nullptr, 10);
            }
        }
        std::printf("seed: %llu%s (another one: --seed N or TRAVERSO_TEST_SEED=N)\n", static_cast<unsigned long long>(m_seed),
                    m_seed == m_default ? ", the default" : "");
    }

    // The seed for the generator whose fixed seed was fixedSeed
    std::uint64_t operator()(std::uint64_t fixedSeed) const
    {
        if (m_seed == m_default) {
            return fixedSeed;
        }
        // splitmix64 of both: different generators stay apart
        std::uint64_t z = m_seed ^ (fixedSeed * 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // The test's own seed, for its main generator
    std::uint64_t seed() const { return (*this)(m_default); }

private:
    std::uint64_t m_default;
    std::uint64_t m_seed;
};
