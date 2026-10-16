// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <string>
#include <utility>
#include <variant>

namespace tapein::core {

enum class ErrorCode {
    invalidArgument,
    malformedInput,
    tooLarge,
    unsupported,
    cryptoFailure,
    ioError,
    notFound,
    invalidState,
};

struct Error {
    ErrorCode code;
    std::string message;
};

inline Error makeError(ErrorCode code, std::string message)
{
    return Error{code, std::move(message)};
}

/// Minimal value-or-error type. The core does not throw across its API.
/// Preconditions (value() when !ok(), error() when ok()) are not checked;
/// callers test ok() first.
template <class T>
class Result {
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {} // NOLINT(google-explicit-constructor)
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {} // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return m_data.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<0>(m_data); }
    [[nodiscard]] const T& value() const& { return std::get<0>(m_data); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(m_data)); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(m_data); }
    [[nodiscard]] Error&& error() && { return std::get<1>(std::move(m_data)); }

    template <class F>
    auto map(F&& f) const& -> Result<decltype(f(std::declval<const T&>()))>
    {
        if (!ok()) {
            return error();
        }
        return f(value());
    }

private:
    std::variant<T, Error> m_data;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)), m_failed(true) {} // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return !m_failed; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const& { return m_error; }
    [[nodiscard]] Error&& error() && { return std::move(m_error); }

private:
    Error m_error{};
    bool m_failed = false;
};

} // namespace tapein::core
