/* Copyright (c) 2017, ARM Limited and Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

using namespace std;

namespace MPD
{

static bool blankString(const string &str)
{
	if (str.empty())
		return true;

	const char *s = str.c_str();

	while (isspace(*s))
		s++;

	return *s == '\0';
}

bool Config::tryToLoadFromFile(const std::string &fname)
{
	ifstream file(fname);

	if (!file.is_open())
	{
		return false;
	}

	while (!file.eof())
	{
		string line;
		if (!getline(file, line))
			break;

		if (line[0] == '#')
			continue;

		if (blankString(line))
			continue;

		// Manually parse here to deal with paths properly.
		auto index = line.find_first_of(' ');
		if (index == string::npos)
			continue;

		auto optionName = line.substr(0, index);
		index = line.find_first_not_of(' ', index);
		if (index == string::npos)
			continue;

		string optionVal;
		if (line[index] == '"') // Substring until next "
		{
			auto endIndex = line.find_first_of('"', index + 1);
			if (endIndex == string::npos)
				continue;
			else
				optionVal = line.substr(index + 1, endIndex - (index + 1));
		}
		else
		{
			// Substring until next space in case we have trailing spaces for whatever reason.
			auto endIndex = line.find_first_of(' ', index);
			if (endIndex == string::npos)
				optionVal = line.substr(index);
			else
				optionVal = line.substr(index, endIndex - index);
		}

		// Find option
		{
			auto it = ints.find(optionName.c_str());
			if (it != ints.end())
			{
				*(it->second.ptrToValue) = stoi(optionVal);
				continue;
			}
		}

		{
			auto it = uints.find(optionName.c_str());
			if (it != uints.end())
			{
				*(it->second.ptrToValue) = stoul(optionVal);
				continue;
			}
		}

		{
			auto it = floats.find(optionName.c_str());
			if (it != floats.end())
			{
				*(it->second.ptrToValue) = stof(optionVal);
				continue;
			}
		}

		{
			auto it = strings.find(optionName.c_str());
			if (it != strings.end())
			{
				*(it->second.ptrToValue) = optionVal;
				continue;
			}
		}

		{
			auto it = bools.find(optionName.c_str());
			if (it != bools.end())
			{
				bool result = false;
				if (optionVal == "true" || optionVal == "on" || optionVal == "1")
					result = true;

				*(it->second.ptrToValue) = result;
				continue;
			}
		}

		MPD_ASSERT("Unknown option");
	}
	return true;
}

void Config::dumpToFile(const std::string &fname) const
{
	ofstream file(fname);

	for (const auto &it : ints)
	{
		file << "# " << it.second.description << "\n";
		file << it.second.name << " " << *it.second.ptrToValue << "\n\n";
	}

	for (const auto &it : uints)
	{
		file << "# " << it.second.description << "\n";
		file << it.second.name << " " << *it.second.ptrToValue << "\n\n";
	}

	for (const auto &it : floats)
	{
		file << "# " << it.second.description << "\n";
		file << it.second.name << " " << *it.second.ptrToValue << "\n\n";
	}

	for (const auto &it : strings)
	{
		file << "# " << it.second.description << "\n";
		file << it.second.name;
		file << " \"";
		file << *it.second.ptrToValue;
		file << "\"\n\n";
	}

	for (const auto &it : bools)
	{
		file << "# " << it.second.description << "\n";
		file << it.second.name << " " << (*it.second.ptrToValue ? "on" : "off") << "\n\n";
	}
}
}
