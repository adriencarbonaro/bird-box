#!/usr/bin/env python

import datetime
import os
import re
import subprocess

VERSION_FILE   = os.path.join("main", "version.h")

# Regex patterns
SEMVER_PATTERN = "^v(?P<semver>(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?)$"
DIRTY_PATTERN  = "^.*dirty"
COMMIT_GAP     = "^.*(?P<gap>[1-9]+)-g"

# Commands
GIT_DESCRIBE  = ["git", "describe"]
GIT_REV_PARSE = ["git", "rev-parse", "HEAD"]
COMMON_FLAGS  = ["--always", "--tags"]
SEMVER_FLAGS  = COMMON_FLAGS + ["--abbrev=0"]
FULL_FLAGS    = COMMON_FLAGS + ["--dirty"]

def execCommand(args: list):
  return subprocess.check_output(args)

def decodeOutput(output: str):
  return output.decode('utf-8').replace("\n", "").replace("\r", "")

def makeVersion():
  print("updating version...")

  semver       = decodeOutput(execCommand(GIT_DESCRIBE + SEMVER_FLAGS))
  full_version = decodeOutput(execCommand(GIT_DESCRIBE + FULL_FLAGS))
  build        = decodeOutput(execCommand(GIT_REV_PARSE))

  semver_match = re.match(SEMVER_PATTERN, semver)
  dirty_match  = re.match(DIRTY_PATTERN, full_version)
  commit_match = re.match(COMMIT_GAP, full_version)

  if semver_match is not None:
    VERSION         = str(semver_match.group("semver") or "")
    MAJOR           = str(semver_match.group("major") or "0")
    MINOR           = str(semver_match.group("minor") or "0")
    CONFIG          = str(semver_match.group("patch") or "0")
    PRERELEASE      = str(semver_match.group("prerelease") or "")
    BUILD_METADATA  = str(semver_match.group("buildmetadata") or "")
  else:
    VERSION         = ""
    MAJOR           = "0"
    MINOR           = "0"
    CONFIG          = "0"
    PRERELEASE      = ""
    BUILD_METADATA  = ""

  DIRTY             = str(int(dirty_match != None))

  if commit_match:
    GAP             = str(commit_match.group("gap") or "0")
  else:
    GAP             = "0"

  print("  version: {} - {}".format(VERSION, build))

  date = datetime.datetime.now().isoformat(sep=" ", timespec="seconds")
  header_content = """/** ---------------------------------------------------------
* @file %s
*
* @warning Generated automatically -- DO NOT EDIT MANUALLY
* @date    %s
* @author  Adrien Carbonaro
* ---------------------------------------------------------- */\n\n""" \
  %(VERSION_FILE, date)

  core_template = """#ifndef VERSION_H_
#define VERSION_H_

#include <stdint.h>

typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} app_version_t;

#define VERSION               "__VERSION__"
#define BUILD_ID              "__BUILD_ID__"
#define BUILD_ID_SHORT        "__BUILD_ID_SHORT__"

#define MAJOR                 (__MAJOR__)
#define MINOR                 (__MINOR__)
#define PATCH                 (__PATCH__)

#define PRERELEASE            "__PRERELEASE__"
#define BUILD_METADATA        "__BUILD_METADATA__"

#define COMMIT_GAP            (__COMMIT_GAP__)
#define DIRTY                 (__DIRTY__)

#endif /* VERSION_H_ */
"""

  core_content = core_template \
    .replace("__VERSION__", VERSION) \
    .replace("__BUILD_ID__", build) \
    .replace("__BUILD_ID_SHORT__", build[:6]) \
    .replace("__MAJOR__",  MAJOR) \
    .replace("__MINOR__",  MINOR) \
    .replace("__PATCH__", CONFIG) \
    .replace("__PRERELEASE__", PRERELEASE) \
    .replace("__BUILD_METADATA__", BUILD_METADATA) \
    .replace("__COMMIT_GAP__", GAP) \
    .replace("__DIRTY__", DIRTY) \

  file_content = header_content + core_content

  if os.path.isfile(VERSION_FILE):
    os.remove(VERSION_FILE)

  print("  writing file:", VERSION_FILE)
  f = open(VERSION_FILE, "w")
  f.write(file_content)
  f.close()

  print("done")

  return (VERSION, build)

if __name__ == "__main__":
  makeVersion()
