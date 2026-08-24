// This file is part of Notepad++ project
// Copyright (C)2026 Don HO <don.h@free.fr>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace NppFormatOnSave
{

enum class DocumentLanguage
{
	Unsupported,
	Json,
	Xml
};

enum class FormatStatus
{
	UnsupportedLanguage,
	TooLarge,
	MixedLineEndings,
	InvalidDocument,
	Unchanged,
	Formatted
};

struct FormatResult final
{
	FormatStatus status = FormatStatus::Unchanged;
	std::string text;

	[[nodiscard]] bool wasFormatted() const noexcept
	{
		return status == FormatStatus::Formatted;
	}
};

// Formatting is deliberately restricted to parser-backed document formats. This
// function never performs heuristic formatting and leaves invalid input unchanged.
// The result preserves a document's consistent EOL convention and whether it ends
// with an EOL. Documents larger than this limit are skipped to keep saving responsive.
constexpr std::size_t maxDocumentBytes = 8U * 1024U * 1024U;

[[nodiscard]] FormatResult formatDocument(std::string_view source, DocumentLanguage language);

} // namespace NppFormatOnSave
