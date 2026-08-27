#pragma once

#include <cstdio>
#include <string_view>

namespace arti::test {

// 极简断言收集器：不引入第三方测试框架，退出码即测试结果。
class Checker {
public:
    explicit Checker(std::string_view name)
        : m_name(name)
    {}

    bool check(bool condition, std::string_view expression, std::string_view file, int line)
    {
        ++m_total;
        if (!condition) {
            ++m_failed;
            std::fprintf(stderr, "%.*s: FAILED %.*s (%.*s:%d)\n",
                    static_cast<int>(m_name.size()), m_name.data(),
                    static_cast<int>(expression.size()), expression.data(),
                    static_cast<int>(file.size()), file.data(), line);
        }
        return condition;
    }

    int summary() const
    {
        std::fprintf(stdout, "%.*s: %d/%d checks passed\n", static_cast<int>(m_name.size()),
                m_name.data(), m_total - m_failed, m_total);
        return m_failed == 0 ? 0 : 1;
    }

private:
    std::string_view m_name;
    int m_total{ 0 };
    int m_failed{ 0 };
};

} // namespace arti::test

#define ARTI_CHECK(checker, condition) (checker).check((condition), #condition, __FILE__, __LINE__)
