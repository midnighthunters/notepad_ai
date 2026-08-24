// This file is part of Notepad++ project
// Copyright (C)2026 Don HO <don.h@free.fr>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.

#include "FormatOnSave.h"

#include <sstream>
#include <exception>
#include <utility>

#include "json.hpp"
#include "pugixml.hpp"

namespace NppFormatOnSave
{
namespace
{

enum class Eol
{
	None,
	Lf,
	Crlf,
	Cr,
	Mixed
};

[[nodiscard]] Eol detectEol(std::string_view text)
{
	Eol detected = Eol::None;
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		Eol current = Eol::None;
		if (text[index] == '\r')
		{
			current = index + 1 < text.size() && text[index + 1] == '\n' ? Eol::Crlf : Eol::Cr;
			if (current == Eol::Crlf)
				++index;
		}
		else if (text[index] == '\n')
		{
			current = Eol::Lf;
		}

		if (current == Eol::None)
			continue;
		if (detected == Eol::None)
			detected = current;
		else if (detected != current)
			return Eol::Mixed;
	}
	return detected;
}

[[nodiscard]] bool hasTerminalEol(std::string_view text)
{
	return !text.empty() && (text.back() == '\n' || text.back() == '\r');
}

void removeTerminalEol(std::string& text)
{
	if (text.empty())
		return;
	if (text.back() == '\n')
	{
		text.pop_back();
		if (!text.empty() && text.back() == '\r')
			text.pop_back();
	}
	else if (text.back() == '\r')
	{
		text.pop_back();
	}
}

void normalizeEol(std::string& text, Eol target)
{
	std::string normalized;
	normalized.reserve(text.size());
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		if (text[index] == '\r')
		{
			if (index + 1 < text.size() && text[index + 1] == '\n')
				++index;
			normalized.push_back('\n');
		}
		else
		{
			normalized.push_back(text[index]);
		}
	}

	if (target == Eol::Crlf)
	{
		std::string converted;
		converted.reserve(normalized.size() + normalized.size() / 8);
		for (const char character : normalized)
		{
			if (character == '\n')
				converted.push_back('\r');
			converted.push_back(character);
		}
		text = std::move(converted);
	}
	else if (target == Eol::Cr)
	{
		for (char& character : normalized)
		{
			if (character == '\n')
				character = '\r';
		}
		text = std::move(normalized);
	}
	else
	{
		text = std::move(normalized);
	}
}

void restoreTerminalEol(std::string& text, Eol eol, bool sourceHasTerminalEol)
{
	if (!sourceHasTerminalEol)
	{
		removeTerminalEol(text);
		return;
	}

	if (hasTerminalEol(text))
		return;

	switch (eol)
	{
		case Eol::Crlf: text += "\r\n"; break;
		case Eol::Cr: text.push_back('\r'); break;
		case Eol::Lf:
		case Eol::None: text.push_back('\n'); break;
		case Eol::Mixed: break;
	}
}

[[nodiscard]] FormatResult formatJson(std::string_view source)
{
	const auto document = nlohmann::json::parse(source.begin(), source.end());
	return { FormatStatus::Unchanged, document.dump(4) };
}

[[nodiscard]] FormatResult formatXml(std::string_view source)
{
	pugi::xml_document document;
	const pugi::xml_parse_result parsed = document.load_buffer(
		source.data(), source.size(), pugi::parse_default | pugi::parse_comments | pugi::parse_declaration);
	if (!parsed || !document.document_element())
		return { FormatStatus::InvalidDocument, {} };

	std::ostringstream output;
	document.save(output, "    ", pugi::format_indent | pugi::format_no_declaration);
	return { FormatStatus::Unchanged, output.str() };
}

} // namespace

FormatResult formatDocument(std::string_view source, DocumentLanguage language)
{
	if (language == DocumentLanguage::Unsupported)
		return { FormatStatus::UnsupportedLanguage, {} };
	if (source.size() > maxDocumentBytes)
		return { FormatStatus::TooLarge, {} };

	const Eol eol = detectEol(source);
	if (eol == Eol::Mixed)
		return { FormatStatus::MixedLineEndings, {} };

	try
	{
		FormatResult result;
		switch (language)
		{
			case DocumentLanguage::Json: result = formatJson(source); break;
			case DocumentLanguage::Xml: result = formatXml(source); break;
			case DocumentLanguage::Unsupported: return { FormatStatus::UnsupportedLanguage, {} };
		}

		if (result.status == FormatStatus::InvalidDocument)
			return result;

		normalizeEol(result.text, eol);
		restoreTerminalEol(result.text, eol, hasTerminalEol(source));
		result.status = result.text == source ? FormatStatus::Unchanged : FormatStatus::Formatted;
		return result;
	}
	catch (const std::exception&)
	{
		return { FormatStatus::InvalidDocument, {} };
	}
}

} // namespace NppFormatOnSave
