/*
 * Copyright 2002, Log4cpp Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <log4cpp/threading/Threading.hh>
#include <cstdlib>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(LOG4CPP_HAVE_THREADING) && defined(LOG4CPP_USE_PTHREADS)

namespace log4cpp {
    namespace threading {

        std::string getThreadId() {
            char str[42];
            ::snprintf(str, sizeof(str), "%d:%ld", ::getpid(), static_cast<long>(::syscall(SYS_gettid)));
            return std::string(str);
        }

    }
}

#endif // LOG4CPP_HAVE_THREADING && LOG4CPP_USE_PTHREADS
