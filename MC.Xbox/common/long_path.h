#pragma once

#include <string>

// Returns the extended-length form of a local path ("\\?\" for drive paths,
// "\\?\UNC\" for shares) so the file APIs keep working past MAX_PATH.
//
// Windows still refuses paths longer than 260 characters unless the path itself
// carries the extended-length marker, and the packaged Xbox layout gets close:
// LocalState alone is about 105 characters, and NeoForge pulls in a guava
// artifact whose library path is 153 characters. With the downloader's
// ".download" suffix the temporary file landed at 267 characters, so that one
// library could never be written and the launcher retried the download forever.
//
// Only file API calls use the result. Paths handed to the JVM, to mods, or to
// logs keep the plain form, because the marker turns off the path normalization
// (forward slashes, "." / ".." segments, doubled or trailing separators) those
// callers expect. Paths that are relative, already extended, or that need that
// normalization are returned unchanged.
std::wstring ExtendedLengthPath(const std::wstring& path);
