#pragma once

#include <optional>
#include <string>
#include <utility>

namespace artcade::sfx {

enum class ErrorCode {
    None,
    InvalidSampleRate,
    InvalidDuration,
    InvalidEnvelope,
    InvalidFrequency,
    InvalidDutyCycle,
    InvalidGain,
    InvalidBitCrusher,
    TooManyFrames,
    EmptyAudio,
    FileOpenFailed,
    FileWriteFailed,
    EncoderUnavailable,
    EncoderFailure,
    InvalidRecipe,
    InvalidArgument
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
};

template <typename T>
class Result {
public:
    static Result success(T value) {
        Result result;
        result.value_ = std::move(value);
        return result;
    }

    static Result failure(ErrorCode code, std::string message) {
        Result result;
        result.error_ = Error{code, std::move(message)};
        return result;
    }

    [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const { return value_.value(); }
    [[nodiscard]] T& value() { return value_.value(); }
    [[nodiscard]] T&& takeValue() { return std::move(value_.value()); }

    [[nodiscard]] const Error& error() const noexcept { return error_; }

private:
    std::optional<T> value_;
    Error error_{};
};

} // namespace artcade::sfx
