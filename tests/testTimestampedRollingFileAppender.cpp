#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>

#include "log4cpp/Category.hh"
#include "log4cpp/OstreamAppender.hh"
#include "log4cpp/PatternLayout.hh"
#include "log4cpp/PropertyConfigurator.hh"
#include "log4cpp/TimestampedRollingFileAppender.hh"

#ifdef LOG4CPP_HAVE_IO_H
#include <io.h>
#endif
#ifdef LOG4CPP_HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef WIN32  // only available on Win32
#include <dirent.h>
#else
#include <direct.h>
#endif

#ifdef WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

using namespace log4cpp;
using namespace std;
static const char *const test_message = "message";
static const char *const log_file_prefix = "timestamped_rolling_program.log";
#define NESTEDDIR "nesteddir"
#ifndef WIN32
#define PATHDELIMITER "/"
#else
#define PATHDELIMITER "\\"
#endif
const char *const nested_dir_name = NESTEDDIR PATHDELIMITER;

class TimestampedRollingTest {
  TimestampedRollingFileAppender *app, *nestedDirApp;

 public:
  bool RemoveImpl(const char *filename) {
    int res = std::remove(filename);

    if (res != 0 && errno != ENOENT)
      cout << "Can't remove file '" << filename << "'.\n";

    return res == 0 || (res != 0 && errno == ENOENT);
  }

  bool RemoveFiles() {
    if (!RemoveImpl(log_file_prefix)) return false;

    return true;
  }

  std::string GenerateTimestampStr(std::int64_t timestamp, bool is_iso8601) {
    if (is_iso8601)
      return TimestampedRollingFileAppender::ConvertTimeToIso8601(timestamp);
    return std::to_string(timestamp);
  }

  bool PrepareBackupLogs(const std::string &log_file_path,
                         const std::string &nested_log_file_path,
                         std::int32_t days, std::int32_t count_earlier,
                         std::int32_t count_later) {
    if (log_file_path.empty()) return false;
    if (nested_log_file_path.empty()) return false;

    assert(days >= 0);
    assert(count_earlier >= 0);
    assert(count_later >= 0);

    const std::int64_t threshold =
        TimestampedRollingFileAppender::GetCurrentTimestampInSeconds() -
        days * 24 * 60 * 60;

    std::vector<std::string> timestamp_strs;
    timestamp_strs.reserve(count_earlier + count_later);
    bool is_iso8601 = false;

    std::cout << "count_earlier '" << count_earlier << "'" << std::endl;

    if (count_earlier > 0) {
      for (int i = 1; i <= count_earlier; ++i) {
        std::int64_t timestamp =
            static_cast<std::time_t>(threshold - i * 60 * 60);
        timestamp_strs.emplace_back(
            GenerateTimestampStr(timestamp, (is_iso8601 = !is_iso8601)));
      }
    }

    std::cout << "count_later '" << count_later << "'" << std::endl;

    if (count_later > 0) {
      for (int i = 1; i <= count_later; ++i) {
        std::int64_t timestamp =
            static_cast<std::time_t>(threshold + i * 60 * 60);
        timestamp_strs.emplace_back(
            GenerateTimestampStr(timestamp, (is_iso8601 = !is_iso8601)));
      }
    }

    if (std::any_of(timestamp_strs.begin(), timestamp_strs.end(),
                    [](const std::string &str) { return str.empty(); })) {
      return false;
    }

    for (auto &&str : timestamp_strs) {
      {
        std::string path = log_file_path + "." + str;
        std::ofstream ofs{path};
        if (!ofs.is_open()) {
          std::cerr << "Could not open file '" << path << "'" << std::endl;
          return false;
        }
        ofs << str;
        // std::cout << "path '" << path << "'" << std::endl;
      }

      {
        std::string path = nested_log_file_path + "." + str;
        std::ofstream ofs{path};
        if (!ofs.is_open()) {
          std::cerr << "Could not open file '" << path << "'" << std::endl;
          return false;
        }
        ofs << str;
        // std::cout << "path '" << path << "'" << std::endl;
      }
    }

    return true;
  }

  bool DirectoryExists(const char *dir_path) {
    struct stat info;
    if (::stat(dir_path, &info) != 0) {
      return false;
    } else if (info.st_mode & S_IFDIR) {
      return true;
    } else {
      return false;
    }
  }

  bool CreateDirectory(const char *dir_path) {
    if (!DirectoryExists(dir_path)) {
      if (::mkdir(dir_path, 0755) == 0) {
        std::cout << "Directory created successfully: " << dir_path
                  << std::endl;
        return true;
      } else {
        std::cerr << "Failed to create directory: " << dir_path << std::endl;
        perror("Error");
        return false;
      }
    } else {
      std::cout << "Directory already exists: " << dir_path << std::endl;
      return true;
    }
  }

  bool Setup() {
    if (!RemoveFiles()) return false;

    const std::int64_t max_file_size = 1;
    const std::int32_t max_backup_count = 3;
    const std::int32_t max_backup_days = 1;

    const std::string log_file_name = log_file_prefix;
    const std::string nested_log_file_name =
        std::string(nested_dir_name).append(log_file_prefix);

    if (!CreateDirectory(nested_dir_name)) return false;

    //        max_backup_days
    //                max_backup_count
    //                       now
    //   . .  |  . .  |  . .  |
    if (!PrepareBackupLogs(log_file_name, nested_log_file_name, max_backup_days,
                           2, 1))
      return false;

    Category &root = Category::getRoot();

    app = new TimestampedRollingFileAppender("timestamped-rolling-appender",
                                             log_file_prefix, max_file_size,
                                             max_backup_count, max_backup_days);
    root.addAppender(app);

    nestedDirApp = new TimestampedRollingFileAppender(
        "nesteddir-timestamped-rolling-appender", nested_log_file_name,
        max_file_size, max_backup_count, max_backup_days);
    root.addAppender(nestedDirApp);

    root.setPriority(Priority::DEBUG);

    return true;
  }

  void MakeLogFiles() {
    Category::getRoot().debugStream()
        << "The message before rolling over attempt";
    app->rollOver();
    nestedDirApp->rollOver();
    Category::getRoot().debugStream()
        << "The message after rolling over attempt";
  }

  bool Exists(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
      cout << "File '" << filename << "' doesn't exists.\n";
      return false;
    }

    fclose(f);

    return true;
  }

  bool CheckLogFiles() {
    bool result = Exists(log_file_prefix);

    Category::shutdown();
    return result && RemoveFiles();
  }
};

int TestOnlyTimestampedRollingFileAppender() {
  TimestampedRollingTest test;
  if (!test.Setup()) {
    std::cout << "Setup has failed. Check for permissions on files "
              << log_file_prefix << "*'." << std::endl;
    return -1;
  }

  test.MakeLogFiles();

  if (test.CheckLogFiles())
    return 0;
  else
    return -1;
}

int TestConfigTimestampedRollingFileAppender() {
  /* looking for the init file in $srcdir is a requirement of
     automake's distcheck target.
  */
  const char *srcdir = getenv("srcdir");
  std::string initFileName;
  try {
#if defined(WIN32)
    initFileName = "./testConfig.log4cpp.timestamped_rolling.nt.properties";
#else
    initFileName = "./testConfig.log4cpp.timestamped_rolling.properties";
#endif
    if (srcdir != NULL) {
      initFileName = std::string(srcdir) + PATHDELIMITER + initFileName;
    }

    log4cpp::PropertyConfigurator::configure(initFileName);
  } catch (log4cpp::ConfigureFailure &f) {
    std::cout << "Configure Problem " << f.what()
              << "($srcdir=" << ((srcdir != NULL) ? srcdir : "NULL") << ")"
              << std::endl;
    return -1;
  }

  log4cpp::Category &root = log4cpp::Category::getRoot();

  log4cpp::Category &sub1 = log4cpp::Category::getInstance(std::string("sub1"));

  root.error("root error");
  root.warn("root warn");
  sub1.error("sub1 error");
  sub1.warn("sub1 warn");

  log4cpp::Category::shutdown();
  return 0;
}

int main() {
  int res = TestOnlyTimestampedRollingFileAppender();
  if (!res) {
    res = TestConfigTimestampedRollingFileAppender();
  }

  return res;
}
