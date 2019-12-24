#include "matcher-ast.hpp"
#include <debug.hpp>

#include <map>
#include <set>
#include <regex>
#include <wayfire/util/log.hpp>

namespace wf
{
    namespace matcher
    {
        using std::string;
        using std::vector;

        namespace util
        {
            /* Remove spaces in front of / at the back of the string */
            string trim(string text)
            {
                int i = 0, j = text.length() - 1;
                while (i <= j)
                {
                    if (std::isspace(text[i])) {
                        ++i;
                    } else if (std::isspace(text[j])) {
                        --j;
                    } else {
                        break;
                    }
                }

                return text.substr(i, j - i + 1);
            }

            /* Split the given text at the given delimiters */
            vector<string> tokenize(string text, string delim)
            {
                vector<string> result;
                string buffer = "";

                auto pack_buffer = [&result, &buffer] () {
                    buffer = trim(buffer);
                    if (buffer.length() > 0)
                        result.push_back(buffer);
                    buffer.clear();
                };

                for (char c : text)
                {
                    if (delim.find(c) != string::npos) {
                        pack_buffer();
                    } else {
                        buffer += c;
                    }
                }

                pack_buffer(); // pack the last token
                return result;
            }

        }

        namespace matchers
        {
            using func_t = std::function<bool(string, string)>;
            func_t exact = [] (string text, string pattern)
            {
                if (pattern == "any")
                    return true;

                bool result = false;
                try {
                    result = std::regex_match(text, std::regex(pattern));
                } catch (const std::exception& e) {
                    LOGE ("Invalid regular expression: %s", pattern.c_str());
                }

                return result;
            };

            func_t contains = [] (string text, string pattern)
            {
                return text.find(pattern) != text.npos;
            };

            std::map<string, func_t> matchers = {
                {"is", exact},
                {"contains", contains},
            };
        }

        /* Which attribute of the view we want to match against */
        enum match_field
        {
            FIELD_TITLE,
            FIELD_APP_ID,
            FIELD_TYPE,
            FIELD_FOCUSEABLE,
        };

        std::map<string, match_field> match_fields = {
            {"title", FIELD_TITLE},
            {"app-id", FIELD_APP_ID},
            {"type", FIELD_TYPE},
            {"focuseable", FIELD_FOCUSEABLE},
        };

        /* Represents the lowest-level criterium to match against (i.e no logic operators) */
        struct single_expression_t : public expression_t
        {
            match_field field;
            matchers::func_t matcher;
            string matcher_arg;

            single_expression_t(string expr)
            {
                /* A single expression consists of 3 parts:
                 * <match field> <match mode> <token> */
                auto tokens = util::tokenize(expr, " ");
                if (tokens.size() != 3)
                    throw std::invalid_argument("Invalid single expression: " + expr);

                if (!match_fields.count(tokens[0]))
                    throw std::invalid_argument("Invalid match field: " + tokens[0]);

                if (!matchers::matchers.count(tokens[1]))
                    throw std::invalid_argument("Invalid match mode: " + tokens[1]);

                this->field = match_fields[tokens[0]];
                this->matcher = matchers::matchers[tokens[1]];
                this->matcher_arg = tokens[2];
            }

            bool evaluate(const view_t& view) override
            {
                string view_field_data;
                switch (this->field)
                {
                    case FIELD_TITLE:
                        view_field_data = view.title;
                        break;
                    case FIELD_APP_ID:
                        view_field_data = view.app_id;
                        break;
                    case FIELD_TYPE:
                        view_field_data = view.type;
                        break;
                    case FIELD_FOCUSEABLE:
                        view_field_data = view.focuseable;
                }

                return this->matcher(view_field_data, this->matcher_arg);
            }
        };

        /* Logic operator, sorted by precedence, starting from lowest */
        enum logic_op
        {
            LOGIC_OR = 0,
            LOGIC_AND = 1,
            LOGIC_NOT = 2,
        };

        struct logic_split_result
        {
            string arg0, arg1;
            logic_op op;
        };

        logic_split_result split_at_logical_op(string expr)
        {
            int paren_balance = 0;
            for (int i = 0; i < int(expr.size()) - 1; i++)
            {
                if (expr[i] == '(')
                {
                    ++paren_balance;
                }
                else if (expr[i] == ')')
                {
                    --paren_balance;
                }
                else if (paren_balance == 0)
                {
                    if (expr.substr(i, 2) == "&&" || expr.substr(i, 2) == "||")
                    {
                        return {
                            util::trim(expr.substr(0, i)),
                            util::trim(expr.substr(i + 2, expr.length() - i - 2)),
                            expr.substr(i, 2) == "&&" ? LOGIC_AND : LOGIC_OR,
                        };
                    }
                }
            }

            return {"", "", LOGIC_AND};
        }

        std::unique_ptr<expression_t> parse_expression_throw_on_fail(string expression)
        {
            auto r = parse_expression(expression);
            if (r.first) {
                return std::move(r.first);
            } else {
                throw std::invalid_argument(r.second);
            }
        }

        struct logic_expression_t : public expression_t
        {
            logic_op op;
            std::unique_ptr<expression_t> arg0;
            std::unique_ptr<expression_t> arg1;

            logic_expression_t(string expression)
            {
                /* Possible syntaxes:
                 * 1. !(expr)
                 * 2. (expr && expr)
                 * 3. (expr || expr) */
                expression = util::trim(expression);
                if (expression.empty() || expression.size() <= 4)
                    throw std::invalid_argument("Empty expression");

                if (expression[0] == '!')
                {
                    this->op = LOGIC_NOT;
                    this->arg0 = parse_expression_throw_on_fail(expression.substr(2, expression.size() - 3));
                } else
                {
                    if (expression.front() != '(' || expression.back() != ')')
                        throw std::invalid_argument("Invalid logical expression, must be within ( and )");

                    auto split = split_at_logical_op(expression.substr(1, expression.size() - 2));
                    if (split.arg0.length() == 0 || split.arg1.length() == 0)
                    {
                        throw std::invalid_argument("Empty first or second half of the logical "
                            "expression: " + expression);
                    }

                    this->arg0 = parse_expression_throw_on_fail(split.arg0);
                    this->arg1 = parse_expression_throw_on_fail(split.arg1);
                    this->op = split.op;
                }
            }

            bool evaluate(const view_t& view) override
            {
                switch (this->op)
                {
                    case LOGIC_NOT:
                        return !arg0->evaluate(view);
                    case LOGIC_OR:
                        return arg0->evaluate(view) || arg1->evaluate(view);
                    case LOGIC_AND:
                        return arg0->evaluate(view) && arg1->evaluate(view);
                }

                return false;
            }
        };

        struct any_expression_t : public expression_t
        {
            any_expression_t(string expression)
            {
                auto trimmed = util::trim(expression);
                const std::set<string> valid_values = {
                    "any",
                    "1",
                    "all",
                };

                if (valid_values.count(trimmed) == 0)
                {
                    throw std::invalid_argument(
                        "Expression isn't \"any\", \"1\", or \"all\"");
                }
            }

            bool evaluate(const view_t& view) override
            {
                return true;
            }
        };

        struct none_expression_t : public expression_t
        {
            none_expression_t(string expression)
            {
                auto trimmed = util::trim(expression);
                const std::set<string> valid_values = {
                    "none",
                    "0",
                };

                if (valid_values.count(trimmed) == 0)
                {
                    throw std::invalid_argument("Expression isn't \"none\"");
                }
            }

            bool evaluate(const view_t& view) override
            {
                return false;
            }
        };

        template<class expression_type>
        parse_result_t try_parse(string expression)
        {
            parse_result_t result;
            try {
                result.first = std::make_unique<expression_type>(expression);
            } catch (const std::invalid_argument& error) {
                result.second = error.what();
            }

            return result;
        }

        parse_result_t parse_expression(string expression)
        {
            parse_result_t final_result;

            std::vector<parse_result_t> results;
            results.emplace_back(try_parse<logic_expression_t> (expression));
            results.emplace_back(try_parse<single_expression_t> (expression));
            results.emplace_back(try_parse<any_expression_t> (expression));
            results.emplace_back(try_parse<none_expression_t> (expression));

            for (auto& r : results)
            {
                if (r.first)
                    return std::move(r);

                final_result.second += r.second + "\n";
            }

            return final_result;
        }
    }
}
