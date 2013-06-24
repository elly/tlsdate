#include "src/event.h"
#include "src/test_harness.h"

#include <sys/time.h>

TEST(every) {
	struct event *e = event_every(3);
	time_t then = time(NULL);
	time_t now;
	EXPECT_EQ(e, event_wait(e));
	now = time(NULL);
	EXPECT_GT(now, then + 2);
	event_free(e);
}

TEST(composite_wait) {
	struct event *e0 = event_every(2);
	struct event *e1 = event_every(3);
	struct event *ec = event_composite();
	time_t then = time(NULL);
	time_t now;

	ASSERT_EQ(0, event_composite_add(ec, e0));
	ASSERT_EQ(0, event_composite_add(ec, e1));
	EXPECT_EQ(e0, event_wait(ec));
	EXPECT_EQ(e1, event_wait(ec));
	EXPECT_EQ(e0, event_wait(ec));
	now = time(NULL);
	EXPECT_GT(now, then + 3);
	event_free(ec);
	event_free(e1);
	event_free(e0);
}

TEST_HARNESS_MAIN
