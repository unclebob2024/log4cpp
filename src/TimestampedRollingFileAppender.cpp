/*
 * TimestampedRollingFileAppender.cpp
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <log4cpp/TimestampedRollingFileAppender.hh>
#include <memory>
#include <utility>

#ifdef LOG4CPP_HAVE_SSTREAM
#include <iomanip>
#include <sstream>
#endif

#include "PortabilityImpl.hh"
#ifdef LOG4CPP_HAVE_IO_H
#include <io.h>
#endif
#ifdef LOG4CPP_HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32  // only available on Win32
#include <dirent.h>
#endif

#include <log4cpp/Category.hh>
#include <log4cpp/FactoryParams.hh>

namespace log4cpp {

TimestampedRollingFileAppender::TimestampedRollingFileAppender(
    const std::string &name, const std::string &fileName,
    std::int64_t maxFileSize, std::int32_t maxBackupCount,
    std::int32_t maxBackupDays,
    std::function<bool(const std::string &, std::string &)> compressFunc,
    bool append, mode_t mode)
    : Base{name, fileName, append, mode},
      _maxFileSize{maxFileSize},
      _maxBackupCount{maxBackupCount},
      _maxBackupDays{maxBackupDays},
      _lockFilePath{},
      _compressFunc{compressFunc} {
  if (fileName.empty()) throw std::invalid_argument("fileName is empty");
  if (ExtractFileName(fileName).empty())
    throw std::invalid_argument("fileName lacks file name");

  _lockFilePath = GenerateLockFilePath(fileName);
  if (::creat(_lockFilePath.c_str(), 0644) < 0) {
    std::cerr << "Could not create lock file '" << _lockFilePath << "'. "
              << std::strerror(errno) << std::endl;
    // XXX we got an error, ignore for now
  }
}

void TimestampedRollingFileAppender::_append(
    const log4cpp::LoggingEvent &event) {
  off_t offset = ::lseek(_fd, 0, SEEK_END);
  if (offset < 0) {
    // XXX we got an error, ignore for now
  } else if (static_cast<std::int64_t>(offset) >= _maxFileSize) {
    int lock_fd = -1;
    bool is_locked = false;
    do {
      lock_fd =
          ::open(_lockFilePath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
      if (lock_fd < 0) {
        std::cerr << "Could not open lock file '" << _lockFilePath << "'."
                  << std::strerror(errno);
        break;
      }

      if (::flock(lock_fd, LOCK_EX) != 0) {
        std::cerr << "Could not lock '" << _lockFilePath << "'. "
                  << std::strerror(errno);
        break;
      }
      is_locked = true;

      if (_fd >= 0) {
        if (::close(_fd)) {
          std::cerr << "Could not close file " << _fileName << "."
                    << std::strerror(errno) << std::endl;
          // XXX we got an error, ignore for now
        }
      }
      _fd = ::open(_fileName.c_str(), _flags, _mode);
      if (_fd == -1) {
        std::cerr << "Could not open file " << _fileName << std::endl;
        // XXX we got an error, ignore for now
      }
      offset = ::lseek(_fd, 0, SEEK_END);
      if (offset < 0) {
        // XXX we got an error, ignore for now
      } else {
        if (static_cast<std::int64_t>(offset) >= _maxFileSize) {
          rollOver();
        }
      }
    } while (false);

    if (lock_fd >= 0) {
      if (is_locked) {
        if (::flock(lock_fd, LOCK_UN) != 0) {
          std::cerr << "Could not unlock '" << _lockFilePath << "'. "
                    << std::strerror(errno);
          // XXX we got an error, ignore for now
        }
        is_locked = false;
      }
      if (::close(lock_fd) != 0) {
        std::cerr << "Could not close lock fd '" << lock_fd << "'. "
                  << std::strerror(errno);
      }
      lock_fd = -1;
    }
  } else {
    // Nothing
  }

  FileAppender::_append(event);
}

void TimestampedRollingFileAppender::rollOver() {
  if (::close(_fd)) {
    std::cerr << "Could not close file " << _fileName << "."
              << std::strerror(errno) << std::endl;
  }

  const std::string datetime_now_str = GetDatetimeNowStr();
  assert(!datetime_now_str.empty());

  const std::string last_log_file_name =
      std::string{_fileName}.append(".").append(datetime_now_str);
  if (::rename(_fileName.c_str(), last_log_file_name.c_str())) {
    std::cerr << "Could not rename file '" << _fileName << "' to '"
              << last_log_file_name << "'. " << std::strerror(errno)
              << std::endl;
    // XXX we got an error, ignore for now
  }

  CompressLog(last_log_file_name);

  //                                      ISO 9601                        UTC
  //                                      timestamp
  // list all .ext file, search _fileName.xxxxxxxxTxxxxxxXXX 和
  // _fileName.xxxxxxxxxx
  // <timestamp, file_path>
  std::vector<std::pair<std::int64_t, std::string>> time_based_file_paths =
      FilterTimeBasedFilePaths(_fileName);

  std::sort(time_based_file_paths.begin(), time_based_file_paths.end(),
            [](const std::pair<std::int64_t, std::string> &lhs,
               const std::pair<std::int64_t, std::string> &rhs) {
              return lhs.first < rhs.first;
            });

  // [0, lower_bound) need delete
  // [lower_bound, time_based_file_paths.size()) need reserve
  std::int32_t lower_bound = 0;  // index

  if (_maxBackupCount >= 0) {
    lower_bound = std::max(
        lower_bound,
        std::max(0, static_cast<std::int32_t>(time_based_file_paths.size()) -
                        _maxBackupCount));
  }

  if (_maxBackupDays >= 0) {
    // reserve log whose time_based_file_paths[k].first >= timestamp_now -
    // _maxBackupDays * 24 * 60 * 60
    const std::int64_t threshold =
        GetCurrentTimestampInSeconds() -
        static_cast<std::int64_t>(_maxBackupDays) * 24 * 60 * 60;
    // binary search the index of the first time_based_file_paths[k].first >
    // threshold
    auto it = std::lower_bound(
        time_based_file_paths.begin(), time_based_file_paths.end(),
        std::make_pair(threshold, ""),
        [](const std::pair<std::int64_t, std::string> &lhs,
           const std::pair<std::int64_t, std::string> &rhs) {
          return lhs.first < rhs.first;
        });
    if (it != time_based_file_paths.end()) {
      lower_bound =
          std::max(lower_bound, static_cast<std::int32_t>(std::distance(
                                    time_based_file_paths.begin(), it)));
    } else {
      lower_bound = time_based_file_paths.size();
    }
  }

  assert(lower_bound >= 0);
  for (std::int32_t i = 0; i < lower_bound; ++i) {
    ::unlink(time_based_file_paths[i].second.c_str());
  }

  _fd = ::open(_fileName.c_str(), _flags, _mode);
  if (_fd == -1) {
    std::cerr << "Could not open file " << _fileName << std::endl;
  }
}

std::int64_t TimestampedRollingFileAppender::ConvertDatetimeIso8601ToTimestamp(
    const std::string &s) {
  std::tm tm = ConvertDatetimeIso8601ToTm(s);
  std::time_t time = ::timegm(&tm);
  int offset = ParseTimezoneOffset(s);

  if (s.find('Z') == std::string::npos && s.find('+') == std::string::npos &&
      s.find('-') == std::string::npos) {
    return time;
  }
  return time - offset;
}

std::tm TimestampedRollingFileAppender::ConvertDatetimeIso8601ToTm(
    const std::string &s) {
  std::tm tm = {};
  if (std::sscanf(s.c_str(), "%4d%2d%2dT%2d%2d%2d", &tm.tm_year, &tm.tm_mon,
                  &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
    tm.tm_year -= 1900;  // tm_year is years since 1900
    tm.tm_mon -= 1;      // tm_mon is 0-based
  } else {
    throw std::invalid_argument("s");
  }
  return tm;
}

std::int64_t TimestampedRollingFileAppender::ConvertStrToInt64(
    const std::string &s) {
  if (s.empty()) throw std::invalid_argument{"'s' is empty"};

  if (!std::all_of(s.begin(), s.end(), ::isdigit)) {
    throw std::invalid_argument{"`s` contains non-digit characters"};
  }

  std::int64_t res = 0;
  for (const char c : s) {
    res = res * 10 + (c - '0');
    if (res < 0) throw std::overflow_error("integer overflow");
  }

  return res;
}

std::string TimestampedRollingFileAppender::ConvertTimeToIso8601(
    std::time_t timer) {
  std::tm buf{};
  // `localtime` may not be thread-safe
  if (std::tm *tmp = std::localtime(&timer)) {
    buf = *tmp;
  } else {
    return "";
  }

  char str[40]{};
  if (std::strftime(str, sizeof(str), "%Y%m%dT%H%M%S%z", &buf)) {
    RemoveChar(str, sizeof(str), ':');
  } else {
    throw std::runtime_error("Could not call strftime");
  }

  return std::string{str};
}

bool TimestampedRollingFileAppender::ExistRegularFile(
    const std::string &file_path) {
  if (file_path.empty()) return false;

  struct stat stat;
  if (::stat(file_path.c_str(), &stat) != 0)
    throw std::runtime_error{"Could not stat '" + file_path + "'. " +
                             std::strerror(errno)};

  return S_ISREG(stat.st_mode);
}

std::string TimestampedRollingFileAppender::ExtractDatetimeIso8601(
    const std::string &s) {
  // Expected:
  // .yyyymmddThhmmssZ
  // .yyyymmddThhmmssZ.
  // .yyyymmddThhmmss+dddd
  // .yyyymmddThhmmss+dddd.
  // .yyyymmddThhmmss-dddd
  // .yyyymmddThhmmss-dddd.

  if (s.size() < 17 || s[0] != '.' ||
      !std::all_of(s.begin() + 1, s.begin() + 9, ::isdigit))
    return "";
  if (s[9] != 'T' || !std::all_of(s.begin() + 10, s.begin() + 16, ::isdigit))
    return "";
  const char end_char = s[16];
  if (end_char == 'Z') {
    return (s.size() == 17 || (s.size() > 17 && s[17] == '.')) ? s.substr(1, 16)
                                                               : "";
  } else if (end_char == '+' || end_char == '-') {
    if (s.size() < 21 ||
        !std::all_of(s.begin() + 17, s.begin() + 21, ::isdigit))
      return "";
    return (s.size() == 21 || (s.size() > 21 && s[21] == '.')) ? s.substr(1, 20)
                                                               : "";
  }
  return "";
}

std::string TimestampedRollingFileAppender::ExtractDigits(
    const std::string &s) {
  // .dddddddd
  // .dddddddd.

  if (s.size() < 1 || s[0] != '.') return "";
  std::size_t i = 1;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    ++i;
  }
  int count = i - 1;
  if (count < 8) return "";
  if (i >= s.size()) return s.substr(1);
  if (s[i] != '.') return "";
  return s.substr(1, i - 1);
}

bool TimestampedRollingFileAppender::ExtractTimestamp(const std::string &s,
                                                      std::int64_t &timestamp) {
  if (s.empty()) return false;

  timestamp = 0;

  std::string matched_str;

  matched_str = ExtractDatetimeIso8601(s);
  if (!matched_str.empty()) {
    try {
      timestamp = ConvertDatetimeIso8601ToTimestamp(matched_str);
    } catch (const std::exception &e) {
      return false;
    }
    return true;
  }

  matched_str = ExtractDigits(s);
  if (!matched_str.empty()) {
    try {
      timestamp = ConvertStrToInt64(matched_str);
    } catch (const std::exception &e) {
      return false;
    }
    return true;
  }

  return false;
}

std::string TimestampedRollingFileAppender::ExtractDigits1(
    const std::string &s) {
  // .d+
  if (s.size() <= 1 || s[0] != '.') return "";
  if (std::all_of(s.begin() + 1, s.end(), ::isdigit)) return s.substr(1);
  return "";
}

bool TimestampedRollingFileAppender::ExtractIndex(const std::string &s,
                                                  std::int64_t &index) {
  if (s.empty()) return false;

  index = 0;
  std::string matched_str = ExtractDigits1(s);
  if (!matched_str.empty()) {
    try {
      index = ConvertStrToInt64(matched_str);
    } catch (const std::exception &e) {
      return false;
    }
    return true;
  }

  return false;
}

std::time_t TimestampedRollingFileAppender::GetLastModifiedTimestamp(
    const std::string &file_path) {
  struct stat stat;

  if (::stat(file_path.c_str(), &stat) != 0) {
    std::cerr << "Could not stat file '" << file_path << "'. "
              << std::strerror(errno) << std::endl;
    return -1;
  }

  return stat.st_mtime;
}

std::vector<std::pair<std::int64_t, std::string>>
TimestampedRollingFileAppender::FilterTimeBasedFilePaths(
    const std::string &base_file_path) {
  std::vector<std::pair<std::int64_t, std::string>> res;

  std::string base_dir_path;
  std::string base_file_name;  // the file name of a given file path
  SplitPathIntoDirAndFile(base_file_path, base_dir_path, base_file_name);

  assert(!base_file_name.empty());
  if (base_file_name.empty()) return res;

  ::DIR *dir = ::opendir(base_dir_path.c_str());
  if (!dir) {
    std::cerr << "Could not opendir '" << base_dir_path << "'. "
              << std::strerror(errno) << std::endl;
    return res;
  }

  errno = 0;
  struct dirent *entry = nullptr;
  while ((entry = ::readdir(dir))) {
    if (entry->d_type != DT_REG) continue;

    const std::string entry_name = entry->d_name;
    if (entry_name == "." || entry_name == "..") continue;

    std::int64_t timestamp = 0;
    std::string file_path = "";
    if (HandleBackupLogFile(entry_name, base_dir_path, base_file_name,
                            timestamp, file_path)) {
      res.emplace_back(timestamp, file_path);
    }
  }

  if (errno) {
    std::cerr << "Could not readdir '" << base_dir_path << "'. "
              << std::strerror(errno) << std::endl;
    res.clear();
  }

  ::closedir(dir);
  return res;
}

bool TimestampedRollingFileAppender::HandleBackupLogFile(
    const std::string &file_name, const std::string &base_dir_path,
    const std::string &base_file_name, std::int64_t &timestamp,
    std::string &final_file_path) {
  timestamp = 0;
  final_file_path = "";

  // base_file_name should be a prefix of file_name
  if (file_name.size() <= base_file_name.size() ||
      file_name.compare(0, base_file_name.size(), base_file_name))
    return false;

  const std::string file_path =
      std::string(base_dir_path).append("/").append(file_name);
  if (!ExistRegularFile(file_path)) return false;

  if (ExtractTimestamp(file_name.substr(base_file_name.size()), timestamp)) {
    final_file_path = file_path;
    return true;
  }

  std::cerr << "Could not extract timestmap from file name '" << file_path
            << "'" << std::endl;

  // rename legacy log files
  std::int64_t index = 0;
  if (ExtractIndex(file_name.substr(base_file_name.size()), index)) {
    return HandleLegacyLogFile(file_path, timestamp, final_file_path);
  }

  std::cerr << "Could not extract index from '" << file_path << "'"
            << std::endl;

  return false;
}

bool TimestampedRollingFileAppender::HandleLegacyLogFile(
    const std::string &file_path, std::int64_t &timestamp,
    std::string &final_file_path) {
  timestamp = GetLastModifiedTimestamp(file_path);
  if (timestamp < 0) {
    std::cerr << "Could not get last modified timestamp from '" << file_path
              << "'" << std::endl;
    // XXX we got an error, ignore for now
    return false;
  }

  const std::string new_file_path =
      std::string(_fileName).append(".").append(GetDatetimeStr(timestamp));
  if (::rename(file_path.c_str(), new_file_path.c_str())) {
    std::cerr << "Could not rename file '" << file_path << "' to '"
              << new_file_path << "'. " << std::strerror(errno) << std::endl;
    // XXX we got an error, ignore for now
    return false;
  }

  if (CompressLog(new_file_path, final_file_path)) {
  } else {
    final_file_path = new_file_path;
  }
  return true;
}

bool TimestampedRollingFileAppender::CompressLog(
    const std::string &log_file_path, std::string &compressed_file_path) {
  compressed_file_path = "";

  if (!_compressFunc) {
    compressed_file_path = log_file_path;
    return true;
  }

  if (!_compressFunc(log_file_path, compressed_file_path)) {
    std::cerr << "Could not compress log file '" << log_file_path << "' to '"
              << compressed_file_path << "'" << std::endl;
    if (::remove(compressed_file_path.c_str())) {
      std::cerr << "Could not remove compressed log file '"
                << compressed_file_path << "'. " << std::strerror(errno)
                << std::endl;
    }
    return false;
  }

  if (::remove(log_file_path.c_str())) {
    std::cerr << "Could not remove log file '" << log_file_path << "'. "
              << std::strerror(errno) << std::endl;
  }
  return true;
}

std::string TimestampedRollingFileAppender::GenerateLockFilePath(
    const std::string &file_path) {
  std::string dir = "";
  std::string file = "";
  SplitPathIntoDirAndFile(file_path, dir, file);
  return dir + "/." + file + ".lock";
}

std::string TimestampedRollingFileAppender::GetDatetimeNowStr() {
  return GetDatetimeStr(GetCurrentTimestampInSeconds());
}

std::string TimestampedRollingFileAppender::GetDatetimeStr(std::time_t timer) {
  std::string datetime_now_str = ConvertTimeToIso8601(timer);
  if (datetime_now_str.empty()) return std::to_string(timer);
  return datetime_now_str;
}

int TimestampedRollingFileAppender::ParseTimezoneOffset(const std::string &s) {
  size_t pos = s.find('Z');
  if (pos != std::string::npos) return 0;

  pos = s.find_last_of("+-");
  if (pos == std::string::npos) return 0;

  std::string offsetStr = s.substr(pos);
  int hours = std::stoi(offsetStr.substr(1, 2));
  int minutes = std::stoi(offsetStr.substr(4, 2));
  int offset = hours * 3600 + minutes * 60;
  if (offsetStr[0] == '-') {
    offset = -offset;
  }
  return offset;
}

void TimestampedRollingFileAppender::RemoveChar(char *s, std::size_t length,
                                                char c) {
  int index = 0;
  for (std::size_t i = 0; i < length; ++i) {
    if (s[i] != c) {
      s[index++] = s[i];
    }
  }
  s[index] = '\0';
}

void TimestampedRollingFileAppender::SplitPathIntoDirAndFile(
    const std::string &file_path, std::string &dir_path,
    std::string &file_name) {
  std::size_t pos = file_path.find_last_of('/');
  if (pos == file_path.npos) {
    dir_path = ".";
    file_name = file_path;
  } else {
    dir_path = file_path.substr(0, pos);
    file_name = file_path.substr(pos + 1);
  }
}

}  // namespace log4cpp
