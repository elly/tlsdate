/*
 * tlsdated-unittest.c - tlsdated unit tests
 * Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"

#include "src/test_harness.h"
#include "src/tlsdate.h"
#include "src/util.h"

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

FIXTURE(tempdir) {
  char path[PATH_MAX];
};

FIXTURE_SETUP(tempdir) {
  char *p;
  strncpy(self->path, "/tmp/tlsdated-unit-XXXXXX", sizeof(self->path));
  p = mkdtemp(self->path);
  ASSERT_NE(NULL, p);
}

FIXTURE_TEARDOWN(tempdir) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s/load", self->path);
  unlink(buf);
  snprintf(buf, sizeof(buf), "%s/save", self->path);
  unlink(buf);
  ASSERT_EQ(0, rmdir(self->path));
}

int write_time(const char *path, time_t time) {
  int fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0600);
  if (fd == -1)
    return 1;
  if (write(fd, &time, sizeof(time)) != sizeof(time)) {
    close(fd);
    return 1;
  }
  return close(fd);
}

int read_time(const char *path, time_t* time) {
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    return 1;
  if (read(fd, time, sizeof(*time)) != sizeof(*time)) {
    close(fd);
    return 1;
  }
  return close(fd);
}

TEST(sane_time) {
  ASSERT_EQ(0, is_sane_time(0));
  ASSERT_EQ(0, is_sane_time(INT_MAX));
}

TEST(sane_host_time) {
  ASSERT_EQ(1, is_sane_time(time(NULL)));
}

TEST_F(tempdir, load_time) {
  char buf[PATH_MAX];
  time_t tm = 3;
  time_t now = time(NULL);
  snprintf(buf, sizeof(buf), "%s/load", self->path);

  ASSERT_EQ(0, write_time(buf, 0));
  ASSERT_EQ(-1, load_disk_timestamp(buf, &tm));
  ASSERT_EQ(3, tm);

  ASSERT_EQ(0, write_time(buf, INT_MAX));
  ASSERT_EQ(-1, load_disk_timestamp(buf, &tm));
  ASSERT_EQ(3, tm);

  ASSERT_EQ(0, write_time(buf, now));
  ASSERT_EQ(0, truncate(buf, 2));
  ASSERT_EQ(-1, load_disk_timestamp(buf, &tm));
  ASSERT_EQ(3, tm);

  ASSERT_EQ(0, unlink(buf));
  ASSERT_EQ(-1, load_disk_timestamp(buf, &tm));
  ASSERT_EQ(3, tm);

  ASSERT_EQ(0, write_time(buf, now));
  ASSERT_EQ(0, load_disk_timestamp(buf, &tm));
  ASSERT_EQ(now, tm);
}

TEST_F(tempdir, save_time) {
  char buf[PATH_MAX];
  time_t now = time(NULL);
  time_t tm;
  snprintf(buf, sizeof(buf), "%s/save", self->path);

  save_disk_timestamp(buf, now);
  ASSERT_EQ(0, read_time(buf, &tm));
  EXPECT_EQ(now, tm);
}

TEST(tlsdate_tests) {
  struct source source = {
    .next = NULL,
    .host = "<host>",
    .port = "<port>",
    .proxy = "<proxy>"
  };
  char *args[] = { "/nonexistent", NULL, NULL };
  struct opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.sources = &source;
  opts.base_argv = args;
  opts.subprocess_timeout = 1;
  extern char **environ;
  EXPECT_EQ(1, tlsdate(&opts, environ));
  args[0] = "/bin/false";
  EXPECT_EQ(1, tlsdate(&opts, environ));
  args[0] = "/bin/true";
  EXPECT_EQ(0, tlsdate(&opts, environ));
  args[0] = "src/test/sleep-wrap";
  args[1] = "3";
  EXPECT_EQ(-1, tlsdate(&opts, environ));
  opts.subprocess_timeout = 5;
  EXPECT_EQ(0, tlsdate(&opts, environ));
}

TEST(jitter) {
  int i = 0;
  int r;
  const int kBase = 100;
  const int kJitter = 25;
  int nonequal = 0;
  for (i = 0; i < 1000; i++) {
    r = add_jitter(kBase, kJitter);
    EXPECT_GE(r, kBase - kJitter);
    EXPECT_LE(r, kBase + kJitter);
    if (r != kBase)
      nonequal++;
  }
  EXPECT_NE(nonequal, 0);
}

TEST(rotate_hosts) {
  struct source s2 = {
    .next = NULL,
    .host = "host2",
    .port = "port2",
    .proxy = "proxy2"
  };
  struct source s1 = {
    .next = &s2,
    .host = "host1",
    .port = "port1",
    .proxy = "proxy1"
  };
  struct opts opts;
  char *args[] = { "src/test/rotate", NULL };
  memset(&opts, 0, sizeof(opts));
  opts.sources = &s1;
  opts.base_argv = args;
  opts.subprocess_timeout = 2;
  extern char **environ;
  EXPECT_EQ(1, tlsdate(&opts, environ));
  EXPECT_EQ(2, tlsdate(&opts, environ));
  EXPECT_EQ(1, tlsdate(&opts, environ));
  EXPECT_EQ(2, tlsdate(&opts, environ));
}

TEST(proxy_override) {
  struct source s1 = {
    .next = NULL,
    .host = "host",
    .port = "port",
    .proxy = NULL,
  };
  struct opts opts;
  char *args[] = { "src/test/proxy-override", NULL };
  memset(&opts, 0, sizeof(opts));
  opts.sources = &s1;
  opts.base_argv = args;
  opts.subprocess_timeout = 2;
  extern char **environ;
  EXPECT_EQ(1, tlsdate(&opts, environ));
  s1.proxy = "socks5://bad.proxy";
  EXPECT_EQ(2, tlsdate(&opts, environ));
  opts.proxy = "socks5://good.proxy";
  EXPECT_EQ(0, tlsdate(&opts, environ));
}

FIXTURE(mock_platform) {
  struct platform platform;
  struct platform *old_platform;
};

FIXTURE_SETUP(mock_platform) {
  self->old_platform = platform;
  self->platform.rtc_open = NULL;
  self->platform.rtc_write = NULL;
  self->platform.rtc_read = NULL;
  self->platform.rtc_close = NULL;
  platform = &self->platform;
}

FIXTURE_TEARDOWN(mock_platform) {
  platform = self->old_platform;
}

TEST(tlsdate_args) {
  struct source s1 = {
    .next = NULL,
    .host = "host",
    .port = "port",
    .proxy = "proxy",
  };
  struct opts opts;
  char *args[] = { "src/test/return-argc", NULL };
  memset(&opts, 0, sizeof(opts));
  opts.sources = &s1;
  opts.base_argv = args;
  opts.subprocess_timeout = 2;
  opts.leap = 1;
  verbose = 1;
  EXPECT_EQ(9, tlsdate(&opts, environ));
}

TEST_F(mock_platform, sync_hwclock) {
  struct timeval now;
  void *fake_handle = (void *)&now;
  int done = 0;
  now.tv_sec = 12345678;
  int test_time_get(struct timeval *tv) {
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_usec;
    return 0;
  }
  int test_rtc_write(void *handle, const struct timeval *tv) {
    ASSERT_EQ(tv->tv_sec, now.tv_sec);
    ASSERT_EQ(handle, fake_handle);
    done++;
    return 0;
  }
  self->platform.time_get = test_time_get;
  self->platform.rtc_write = test_rtc_write;
  sync_hwclock(fake_handle);
  ASSERT_EQ(done, 1);
}

TEST_F(mock_platform, sync_and_save) {
  struct timeval now;
  void *fake_handle = (void *)&now;
  int done = 0;
  now.tv_sec = 12345678;
  int test_time_get(struct timeval *tv) {
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_usec;
    return 0;
  }
  int test_rtc_write(void *handle, const struct timeval *tv) {
    ASSERT_EQ(tv->tv_sec, now.tv_sec);
    ASSERT_EQ(handle, fake_handle);
    done++;
    return 0;
  }
  int test_file_write(const char *path, void *buf, size_t sz) {
    ASSERT_STREQ(path, timestamp_path);
    done++;
    return 0;
  }
  self->platform.time_get = test_time_get;
  self->platform.rtc_write = test_rtc_write;
  self->platform.file_write = test_file_write;
  sync_and_save(fake_handle, 1);
  ASSERT_EQ(done, 2);
  done = 0;
  sync_and_save(fake_handle, 0);
  ASSERT_EQ(done, 1);
}

TEST_HARNESS_MAIN
