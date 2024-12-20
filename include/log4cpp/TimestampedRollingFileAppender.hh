/*
 * TimestampedRollingFileAppender.hh
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef _LOG4CPP_TIMEDROTATINGCOMPRESSEDFILEAPPENDER_HH
#define _LOG4CPP_TIMEDROTATINGCOMPRESSEDFILEAPPENDER_HH

#include <chrono>
#include <cstdarg>
#include <ctime>
#include <functional>
#include <log4cpp/Portability.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <string>

namespace log4cpp {
/**
 * @brief Appends log messages to a file, rotates and compresses old log files
 * based on size and retention policies.
 *
 * The TimestampedRollingFileAppender handles log messages by appending them to
 * a current log file. When the current log file reaches a specified size, it
 * rotates the log files by renaming it with a timestamp and compressing the
 * current file. It manages the retention of compressed log files based on both
 * the maximum retention period and the maximum number of files to retain.
 *
 * @since 1.1.4
 */
class LOG4CPP_EXPORT TimestampedRollingFileAppender
    : public RollingFileAppender {
  using Base = RollingFileAppender;

 public:
  TimestampedRollingFileAppender(
      const std::string &name, const std::string &fileName,
      std::int64_t maxFileSize = 10 * 1024 * 1024,
      std::int32_t maxBackupCount = 2, std::int32_t maxBackupDays = 30,
      std::function<bool(const std::string &, std::string &)> compressFunc =
          nullptr,
      bool append = true, mode_t mode = 00644);

  static std::string ConvertTimeToIso8601(
      std::time_t timer = std::time(nullptr));

  static inline std::int64_t GetCurrentTimestampInSeconds() {
    return std::time(nullptr);
  }

  void rollOver() override;

 protected:
  static std::int64_t ConvertDatetimeIso8601ToTimestamp(const std::string &s);

  static std::tm ConvertDatetimeIso8601ToTm(const std::string &s);

  static std::int64_t ConvertStrToInt64(const std::string &s);

  static bool ExistRegularFile(const std::string &file_path);

  static std::string ExtractDatetimeIso8601(const std::string &s);

  static std::string ExtractDigits(const std::string &str);

  static inline std::string ExtractFileName(const std::string &path) {
    std::size_t pos = path.find_last_of('/');
    return (pos == path.npos) ? path : path.substr(pos + 1);
  }

  static bool ExtractTimestamp(const std::string &s, std::int64_t &timestamp);

  static std::string ExtractDigits1(const std::string &s);

  static bool ExtractIndex(const std::string &s, std::int64_t &index);

  static std::time_t GetLastModifiedTimestamp(const std::string &file_path);

  std::vector<std::pair<std::int64_t, std::string>> FilterTimeBasedFilePaths(
      const std::string &file_path);

  static std::string GenerateLockFilePath(const std::string &file_path);

  static std::string GetDatetimeNowStr();

  static std::string GetDatetimeStr(std::time_t timer);

  static int ParseTimezoneOffset(const std::string &s);

  static void RemoveChar(char *s, std::size_t length, char c);

  static void SplitPathIntoDirAndFile(const std::string &file_path,
                                      std::string &dir_path,
                                      std::string &file_name);

  void _append(const log4cpp::LoggingEvent &event) override;

  bool CompressLog(const std::string &log_file_path,
                   std::string &compressed_file_path);

  inline bool CompressLog(const std::string &log_file_path) {
    std::string compressed_file_path = "";
    return CompressLog(log_file_path, compressed_file_path);
  }

  /**
   * @param file_name[in] foobar.log.INDEX, foobar.log.TIMESTAMP
   * @param base_dir_path[in] /path/to
   * @param base_file_name[in] foobar.log
   * @param timestamp[out]
   * @param final_file_path[out] /path/to/foobar.log.TIMESTAMP[.gz]
   */
  bool HandleBackupLogFile(const std::string &file_name,
                           const std::string &base_dir_path,
                           const std::string &base_file_name,
                           std::int64_t &timestamp,
                           std::string &final_file_path);

  bool HandleLegacyLogFile(const std::string &file_path,
                           std::int64_t &timestamp,
                           std::string &final_file_path);

 private:
  std::int64_t _maxFileSize;
  /** for backup files */
  std::int32_t _maxBackupCount;
  /** for backup files */
  std::int32_t _maxBackupDays;
  std::string _lockFilePath;

  /**
   * new log4cpp::TimestampedRollingFileAppender{
   * lc->get_appender_name(),
   * log_file_path,
   * max_file_size,
   * max_backup_index,
   * max_backup_days,
   * [](const std::string &from, std::string &to) {
   *   return CompressLogToGzipCallback(from, to);
   * }};
   *
   * bool CompressLogToGzipCallback(const std::string &from, std::string &to) {
   *   // NOTE: MUST NOT call the logger within the current function and its
   *   // invoked functions, as it may cause a deadlock in log4cpp.
   *
   *   assert(!from.empty());
   *   assert(to.empty());
   *
   *   to = from + ".gz";
   *
   *   // compress ...
   *
   *   return true;
   * }
   */
  std::function<bool(const std::string &log_file_path,
                     std::string &compressed_file_path)>
      _compressFunc;
};
}  // namespace log4cpp

#endif  // _LOG4CPP_TIMEDROTATINGCOMPRESSEDFILEAPPENDER_HH
