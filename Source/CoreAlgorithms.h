#pragma once
// Starting routings for the Core Kernel's modulation matrix (in the spirit of the classic six-operator
// algorithms, not copies of any one synth's numbering). Picking one writes the matrix: the connections
// get an amount, the carriers an output; feedback and everything else is left as it was.
namespace tg
{

struct CoreAlgorithm
{
    const char* name;
    const char* edges;      // "65 54" = op 6 modulates op 5, op 5 modulates op 4 ...
    const char* carriers;   // "14" = ops 1 and 4 are heard
};

inline const CoreAlgorithm kCoreAlgorithms[] = {
    { "Stack: 6>5>4>3>2>1",              "65 54 43 32 21", "1" },
    { "Two stacks: 2>1 + 6>5>4>3",       "21 65 54 43",    "13" },
    { "Two stacks: 3>2>1 + 6>5>4",       "32 21 65 54",    "14" },
    { "Three pairs: 2>1, 4>3, 6>5",      "21 43 65",       "135" },
    { "Y pairs: 2+3>1, 5+6>4",           "21 31 54 64",    "14" },
    { "Tree: 2>1, 4>3>1, 6>5>1",         "21 43 31 65 51", "1" },
    { "Fan: 6 drives 3, 4, 5; 2>1",      "63 64 65 21",    "1345" },
    { "One pair + four sines: 6>5",      "65",             "12345" },
    { "Bass: 3>2>1, 5>4>1, 6>1",         "32 21 54 41 61", "1" },
    { "Double top: 5+6>4>3>2>1",        "54 64 43 32 21", "1" },
    { "Brass: 3+4>2>1, 6>5",             "32 42 21 65",    "15" },
    { "Organ: all six heard",            "",               "123456" },
};
constexpr int kNumCoreAlgorithms = (int) (sizeof (kCoreAlgorithms) / sizeof (kCoreAlgorithms[0]));

} // namespace tg
