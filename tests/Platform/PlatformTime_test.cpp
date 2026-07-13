#include <gtest/gtest.h>
#include <Platform/PlatformTime.h>

#include <thread>

TEST(PlatformTimeTest, TickMsIsMonotonic)
{
	const uint32_t first = PlatformTickMs();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	const uint32_t second = PlatformTickMs();

	EXPECT_GE(second, first + 4);
}

TEST(PlatformTimeTest, SecondsAdvanceWithTicks)
{
	const double startSeconds = PlatformTimeSeconds();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	const double endSeconds = PlatformTimeSeconds();

	EXPECT_GT(endSeconds, startSeconds);
	EXPECT_LT(endSeconds - startSeconds, 5.0); // sanity: same clock epoch
}
