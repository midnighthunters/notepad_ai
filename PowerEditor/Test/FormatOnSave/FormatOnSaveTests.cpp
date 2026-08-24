// This file is part of Notepad++ project
// Copyright (C)2026 Don HO <don.h@free.fr>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.

#include "FormatOnSave.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using namespace NppFormatOnSave;

class TestFailure final : public std::runtime_error
{
public:
	explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

void require(bool condition, std::string_view expression, std::string_view test)
{
	if (!condition)
		throw TestFailure(std::string(test) + ": requirement failed: " + std::string(expression));
}

#define REQUIRE(test, expression) require(static_cast<bool>(expression), #expression, test)

void testJsonFormatting()
{
	constexpr std::string_view test = "strict JSON formatting";
	const FormatResult result = formatDocument("{\"name\":\"Müller\",\"values\":[1,true,null]}", DocumentLanguage::Json);
	REQUIRE(test, result.wasFormatted());
	REQUIRE(test, result.text == "{\n    \"name\": \"Müller\",\n    \"values\": [\n        1,\n        true,\n        null\n    ]\n}");
	REQUIRE(test, formatDocument(result.text, DocumentLanguage::Json).status == FormatStatus::Unchanged);
}

void testLineEndingAndTerminalNewlinePreservation()
{
	constexpr std::string_view test = "EOL preservation";
	const FormatResult crlf = formatDocument("{\"a\":1}\r\n", DocumentLanguage::Json);
	REQUIRE(test, crlf.wasFormatted());
	REQUIRE(test, crlf.text == "{\r\n    \"a\": 1\r\n}\r\n");
	const FormatResult cr = formatDocument("{\"a\":1}\r", DocumentLanguage::Json);
	REQUIRE(test, cr.wasFormatted());
	REQUIRE(test, cr.text == "{\r    \"a\": 1\r}\r");
	REQUIRE(test, formatDocument("{\r\n\"a\":1\n}", DocumentLanguage::Json).status == FormatStatus::MixedLineEndings);
}

void testInvalidAndUnsupportedDocumentsAreUntouched()
{
	constexpr std::string_view test = "safe rejection";
	REQUIRE(test, formatDocument("{\"trailing\": true,}", DocumentLanguage::Json).status == FormatStatus::InvalidDocument);
	REQUIRE(test, formatDocument("// JSON5\n{value: 1}", DocumentLanguage::Json).status == FormatStatus::InvalidDocument);
	REQUIRE(test, formatDocument("<root><child></root>", DocumentLanguage::Xml).status == FormatStatus::InvalidDocument);
	REQUIRE(test, formatDocument("body{color:red}", DocumentLanguage::Unsupported).status == FormatStatus::UnsupportedLanguage);
	const std::string oversized(maxDocumentBytes + 1, 'x');
	REQUIRE(test, formatDocument(oversized, DocumentLanguage::Json).status == FormatStatus::TooLarge);
}

void testXmlFormatting()
{
	constexpr std::string_view test = "well-formed XML formatting";
	const FormatResult result = formatDocument("<?xml version=\"1.0\"?><root><!-- note --><item id=\"7\">text</item></root>\n", DocumentLanguage::Xml);
	REQUIRE(test, result.wasFormatted());
	REQUIRE(test, result.text == "<?xml version=\"1.0\"?>\n<root>\n    <!-- note -->\n    <item id=\"7\">text</item>\n</root>\n");
	REQUIRE(test, formatDocument(result.text, DocumentLanguage::Xml).status == FormatStatus::Unchanged);
}

using TestFunction = void (*)();

int runTest(std::string_view name, TestFunction function)
{
	try
	{
		function();
		std::cout << "PASS " << name << '\n';
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
		return 1;
	}
}

} // namespace

int main()
{
	int failures = 0;
	failures += runTest("strict JSON formatting", testJsonFormatting);
	failures += runTest("EOL preservation", testLineEndingAndTerminalNewlinePreservation);
	failures += runTest("safe rejection", testInvalidAndUnsupportedDocumentsAreUntouched);
	failures += runTest("well-formed XML formatting", testXmlFormatting);
	return failures == 0 ? 0 : 1;
}
