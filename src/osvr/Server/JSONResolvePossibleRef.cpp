/** @file
    @brief Implementation

    @date 2015

    @author
    Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2015 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// 	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal Includes
#include "JSONResolvePossibleRef.h"

// Library/third-party includes
#include <json/reader.h>
#include <boost/filesystem.hpp>

// Standard includes
#include <fstream>
#include <string>

using std::make_pair;
using std::tie;

namespace osvr {
namespace server {

    /// @brief Helper function to load a JSON file given a full path to the
    /// file.
    /// @returns a null json value if load failed.
    static inline Json::Value attemptLoad(std::string fullFn) {
        Json::Value ret{Json::nullValue};
        std::ifstream file{fullFn};
        if (!file) {
            return ret;
        }
        Json::Reader reader;
        if (!reader.parse(file, ret)) {
            ret = Json::nullValue;
            return ret;
        }
        return ret;
    }

    static inline std::pair<FileLoadAttempt, Json::Value>
    attemptFileLoad(std::string const &fullFn) {

        Json::Value ret{Json::nullValue};
        auto attempt = FileLoadAttempt{fullFn};

        std::ifstream file{fullFn};
        if (!file) {
            attempt.status = FileLoadStatus::CouldNotOpenFile;
            return make_pair(attempt, ret);
        }

        Json::Reader reader;
        if (!reader.parse(file, ret)) {
            attempt.status = FileLoadStatus::CouldNotParseFile;
            attempt.details = reader.getFormattedErrorMessages();

            ret = Json::nullValue;
            return make_pair(attempt, ret);
        }

        attempt.status = FileLoadStatus::FileOpenedAndParsed;
        return make_pair(attempt, ret);
    }

    /// @brief Helper function to load a JSON file by name in a search path.
    /// @return Json::nullValue if could not load, otherwise parsed contents of
    /// file.
    static inline Json::Value
    loadFromFile(std::string fn, std::vector<std::string> const &searchPath) {
        Json::Value ret{Json::nullValue};
        for (auto const &path : searchPath) {
            auto fullFn = boost::filesystem::path(path) / fn;
            ret = attemptLoad(fullFn.string());
            if (!ret.isNull()) {
                return ret;
            }
        }
        // Last ditch effort, or only attempt if no search path provided
        // This effectively uses the current working directory.
        ret = attemptLoad(fn);
        return ret;
    }

    static inline std::pair<bool, Json::Value>
    loadFromFile(FileLoadAttempts &attempts, std::string const &fn,
                 std::vector<std::string> const &searchPath) {
        Json::Value ret{Json::nullValue};
        for (auto const &path : searchPath) {
            auto fullFn = boost::filesystem::path(path) / fn;
            FileLoadAttempt attempt;
            tie(attempt, ret) = attemptFileLoad(fullFn.string());
            attempts.push_back(attempt);
            if (attempt.status == FileLoadStatus::FileOpenedAndParsed) {
                return make_pair(true, ret);
            }
        }
        // Last ditch effort, or only attempt if no search path provided
        // This effectively uses the current working directory.

        FileLoadAttempt attempt;
        tie(attempt, ret) = attemptFileLoad(fn);
        attempts.push_back(attempt);
        return std::make_pair(
            attempt.status == FileLoadStatus::FileOpenedAndParsed, ret);
    }

    ResolveRefResult
    resolvePossibleRefWithDetails(Json::Value const &input,
                                  bool stringAcceptableResult,
                                  std::vector<std::string> const &searchPath) {
        ResolveRefResult ret;
        ret.result = Json::nullValue;

        if (input.isString()) {
            tie(ret.resolved, ret.result) =
                loadFromFile(ret.fileAttempts, input.asString(), searchPath);
            if (ret.resolved) {
                ret.handledAs = ValueHandledAs::Filename;
                return ret;
            }
            if (stringAcceptableResult) {
                ret.result = input;
                ret.handledAs = ValueHandledAs::String;
            }
            // If given a string, whether or not that's acceptable, we'll be
            // done at the end here.
            return ret;
        }

        if (input.isObject() && input.isMember("$ref")) {
            /// @todo remove things after the filename in the ref.
            tie(ret.resolved, ret.result) = loadFromFile(
                ret.fileAttempts, input["$ref"].asString(), searchPath);
            if (ret.resolved) {
                ret.handledAs = ValueHandledAs::JsonRefToFile;
                return ret;
            }
        }

        ret.result = input;
        return ret;
    }

    Json::Value resolvePossibleRef(Json::Value const &input,
                                   bool stringAcceptableResult,
                                   std::vector<std::string> const &searchPath) {
        Json::Value ret{Json::nullValue};
        if (input.isString()) {
            ret = loadFromFile(input.asString(), searchPath);
            if (ret.isNull() && stringAcceptableResult) {
                ret = input;
            }
            return ret;
        }
        if (input.isObject() && input.isMember("$ref")) {
            /// @todo remove things after the filename in the ref.
            ret = loadFromFile(input["$ref"].asString(), searchPath);
            if (!ret.isNull()) {
                return ret;
            }
        }
        ret = input;
        return ret;
    }

    const char *fileLoadStatusToString(FileLoadStatus status) {
        switch (status) {
        case FileLoadStatus::CouldNotOpenFile:
            return "Could not open file";

        case FileLoadStatus::CouldNotParseFile:
            return "Could not parse file, ";

        case FileLoadStatus::FileOpenedAndParsed:
            return "File opened and parsed";
        }
    }
} // namespace server
} // namespace osvr
