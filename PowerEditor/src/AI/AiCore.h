// This file is part of Notepad++ project
// Copyright (C)2025 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace NppAi
{

enum class AiErrorCode
{
	InvalidArgument,
	InvalidUtf8,
	InvalidRange,
	InvalidLineRange,
	ScopeViolation,
	HashMismatch,
	StaleDocument,
	ParseError,
	SchemaViolation,
	UnknownField,
	OverlappingEdits,
	SizeLimitExceeded,
	ExpansionLimitExceeded,
	EditorRejected,
	TransportRejected,
	TransportFailure,
	HttpFailure,
	Cancelled,
	UnsupportedPlatform,
	SecretUnavailable,
	InternalError
};

struct AiError
{
	AiErrorCode code = AiErrorCode::InternalError;
	std::string message;
};

template <typename T>
class AiResult
{
public:
	AiResult(T value) : _value(std::move(value))
	{
	}

	AiResult(AiError error) : _value(std::move(error))
	{
	}

	[[nodiscard]] bool hasValue() const noexcept
	{
		return std::holds_alternative<T>(_value);
	}

	explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] T & value()
	{
		return std::get<T>(_value);
	}

	[[nodiscard]] const T & value() const
	{
		return std::get<T>(_value);
	}

	[[nodiscard]] AiError & error()
	{
		return std::get<AiError>(_value);
	}

	[[nodiscard]] const AiError & error() const
	{
		return std::get<AiError>(_value);
	}

private:
	std::variant<T, AiError> _value;
};

using AiStatus = AiResult<std::monostate>;

[[nodiscard]] AiStatus AiSuccess();
[[nodiscard]] AiError AiMakeError(AiErrorCode code, std::string message);

struct AiTextRange
{
	std::size_t start = 0;
	std::size_t end = 0;

	[[nodiscard]] bool isOrdered() const noexcept
	{
		return start <= end;
	}

	[[nodiscard]] std::size_t length() const noexcept
	{
		return end - start;
	}
};

[[nodiscard]] bool operator==(const AiTextRange & left, const AiTextRange & right) noexcept;

struct AiEditOperation
{
	AiTextRange range;
	std::string replacement;
	// Derived from the validated snapshot by the parser; checked again before preview/apply.
	std::string expectedTextSha256;
};

struct AiEditPlan
{
	std::uint32_t schemaVersion = 1;
	std::string documentSha256;
	std::string summary;
	std::vector<AiEditOperation> operations;
};

struct AiDocumentSnapshot
{
	std::string text;
	std::string documentSha256;
	std::string documentIdentity;
	std::string language;
	std::uint64_t revision = 0;
};

[[nodiscard]] AiDocumentSnapshot AiMakeDocumentSnapshot(std::string text, std::string documentIdentity = {}, std::string language = {}, std::uint64_t revision = 0);

// Ranges use UTF-8 byte offsets with an exclusive end offset.
enum class AiScopeKind
{
	Selection,
	LineRange,
	Document
};

struct AiLineRange
{
	std::size_t first = 0; // 1-based, inclusive
	std::size_t last = 0;  // 1-based, inclusive
};

struct AiScope
{
	AiScopeKind kind = AiScopeKind::Document;
	AiTextRange range;
	std::optional<AiLineRange> lines;
};

struct AiScopeRequest
{
	// A valid non-empty selection takes precedence over lineRange.
	std::optional<AiTextRange> selection;
	// Parsed only when no valid non-empty selection is present.
	std::optional<std::string> lineRange;
};

// Accepts N or N-M with optional spaces or tabs around the hyphen. Line numbers are 1-based.
[[nodiscard]] AiResult<AiLineRange> AiParseLineRange(std::string_view text, std::size_t lineCount);
[[nodiscard]] AiResult<AiScope> AiResolveScope(const AiDocumentSnapshot & snapshot, const AiScopeRequest & request);

[[nodiscard]] bool AiIsValidUtf8(std::string_view text) noexcept;
[[nodiscard]] bool AiIsUtf8Boundary(std::string_view text, std::size_t byteOffset) noexcept;
[[nodiscard]] std::string AiTruncateUtf8(std::string_view text, std::size_t maximumBytes);

[[nodiscard]] std::string AiSha256Hex(std::string_view text);
[[nodiscard]] bool AiIsSha256Hex(std::string_view text) noexcept;
[[nodiscard]] bool AiConstantTimeEquals(std::string_view left, std::string_view right) noexcept;

enum class AiEol
{
	None,
	Lf,
	Crlf,
	Cr,
	Mixed
};

[[nodiscard]] AiEol AiDetectEol(std::string_view text) noexcept;

struct AiPromptLimits
{
	std::size_t maxInstructionBytes = 8 * 1024;
	std::size_t maxContextBytes = 64 * 1024;
	std::size_t maxPromptBytes = 80 * 1024;
};

struct AiPrompt
{
	std::string system;
	std::string user;
	// This is the exact visible authorization range. Pass it to plan parsing, validation, preview, and apply.
	AiScope includedScope;
	bool contextWasTruncated = false;
	std::size_t omittedContextBytes = 0;
};

// Builds a bounded prompt. Document text is deliberately marked as untrusted data, not instructions.
[[nodiscard]] AiResult<AiPrompt> AiBuildEditPrompt(const AiDocumentSnapshot & snapshot, const AiScope & scope, std::string_view instruction, const AiPromptLimits & limits = {});

struct AiValidationLimits
{
	std::size_t maxPlanBytes = 512 * 1024;
	std::size_t maxJsonNesting = 64;
	std::size_t maxOperations = 128;
	std::size_t maxSummaryBytes = 8 * 1024;
	std::size_t maxReplacementBytesPerOperation = 64 * 1024;
	std::size_t maxTotalReplacementBytes = 256 * 1024;
	std::size_t maxExpansionRatio = 8;
	std::size_t expansionSlackBytes = 4 * 1024;
};

// The strict model-output schema is:
// {"version":1,"document_hash":"...","summary":"...","operations":[
//   {"range":{"start":0,"end":0},"replacement":"..."}
// ]}
// expectedTextSha256 values are derived only after the model ranges have been validated against the
// authoritative snapshot. Markdown ```json fences enclosing exactly one JSON object are tolerated.
[[nodiscard]] AiResult<AiEditPlan> AiParseAndValidateEditPlan(std::string_view modelOutput, const AiDocumentSnapshot & snapshot, const AiScope & allowedScope, const AiValidationLimits & limits = {});
[[nodiscard]] AiStatus AiValidateEditPlan(const AiEditPlan & plan, const AiDocumentSnapshot & snapshot, const AiScope & allowedScope, const AiValidationLimits & limits = {});

enum class AiDiffKind
{
	Insert,
	Delete,
	Replace
};

struct AiDiffHunk
{
	AiDiffKind kind = AiDiffKind::Replace;
	AiTextRange originalRange;
	std::string originalText;
	std::string replacementText;
};

struct AiDiffModel
{
	AiEol documentEol = AiEol::None;
	std::vector<AiDiffHunk> hunks;
};

[[nodiscard]] AiResult<AiDiffModel> AiBuildDiffModel(const AiDocumentSnapshot & snapshot, const AiEditPlan & plan, const AiScope & allowedScope);

// Implementations must preflight ranges without changing text. After beginUndoAction succeeds,
// abortUndoAction must restore the editor to its pre-action state if an operation cannot complete.
class IAiEditor
{
public:
	virtual ~IAiEditor() = default;
	[[nodiscard]] virtual AiResult<AiDocumentSnapshot> captureSnapshot() const = 0;
	[[nodiscard]] virtual AiStatus canReplaceRange(const AiTextRange & range, std::string_view replacement) = 0;
	[[nodiscard]] virtual AiStatus beginUndoAction() = 0;
	virtual void endUndoAction() noexcept = 0;
	virtual void abortUndoAction() noexcept = 0;
	[[nodiscard]] virtual AiStatus replaceRange(const AiTextRange & range, std::string_view replacement) = 0;
};

struct AiApplyResult
{
	std::size_t appliedOperationCount = 0;
};

class AiEditApplier
{
public:
	[[nodiscard]] static AiResult<AiApplyResult> apply(IAiEditor & editor, const AiDocumentSnapshot & snapshot, const AiEditPlan & plan, const AiScope & allowedScope, const AiValidationLimits & limits = {});
};

} // namespace NppAi
