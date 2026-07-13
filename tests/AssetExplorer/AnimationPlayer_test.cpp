#include <gtest/gtest.h>

#include <AnimationPlayer.h>

using namespace assetexplorer;

TEST(AnimationPlayer, SetClipResetsToStart)
{
	AnimationPlayer p;
	p.SetClip(10.0f, 40.0f, 30.0f);
	EXPECT_FLOAT_EQ(p.Frame(), 10.0f);
	EXPECT_FLOAT_EQ(p.StartFrame(), 10.0f);
	EXPECT_FLOAT_EQ(p.EndFrame(), 40.0f);
	EXPECT_FLOAT_EQ(p.FramesPerSecond(), 30.0f);
	EXPECT_FALSE(p.IsPlaying());
}

TEST(AnimationPlayer, PlayAdvancesAtNativeRate)
{
	AnimationPlayer p;
	p.SetClip(0.0f, 30.0f, 30.0f);
	p.Play();
	p.Update(0.5f); // 0.5s * 30fps = 15 frames
	EXPECT_FLOAT_EQ(p.Frame(), 15.0f);
	EXPECT_NEAR(p.Normalized(), 0.5f, 1e-5f);
}

TEST(AnimationPlayer, LoopsPastTheEnd)
{
	AnimationPlayer p;
	p.SetClip(0.0f, 30.0f, 30.0f);
	p.SetLooping(true);
	p.Play();
	p.Update(1.2f); // 36 frames -> wraps to 6
	EXPECT_TRUE(p.IsPlaying());
	EXPECT_NEAR(p.Frame(), 6.0f, 1e-4f);
}

TEST(AnimationPlayer, NonLoopingStopsAtEnd)
{
	AnimationPlayer p;
	p.SetClip(0.0f, 30.0f, 30.0f);
	p.SetLooping(false);
	p.Play();
	p.Update(2.0f); // way past the end
	EXPECT_FALSE(p.IsPlaying());
	EXPECT_FLOAT_EQ(p.Frame(), 30.0f);
}

TEST(AnimationPlayer, SpeedScalesAdvanceAndIsClamped)
{
	AnimationPlayer p;
	p.SetClip(0.0f, 100.0f, 30.0f);
	p.SetSpeed(2.0f);
	EXPECT_FLOAT_EQ(p.Speed(), 2.0f);
	p.Play();
	p.Update(0.5f); // 0.5 * 30 * 2 = 30 frames
	EXPECT_FLOAT_EQ(p.Frame(), 30.0f);

	p.SetSpeed(1000.0f); // clamped
	EXPECT_LE(p.Speed(), 10.0f);
	p.SetSpeed(0.0f); // clamped up
	EXPECT_GE(p.Speed(), 0.01f);
}

TEST(AnimationPlayer, ScrubClampsAndPauses)
{
	AnimationPlayer p;
	p.SetClip(10.0f, 40.0f, 30.0f);
	p.Play();
	p.Scrub(25.0f);
	EXPECT_FLOAT_EQ(p.Frame(), 25.0f);
	EXPECT_FALSE(p.IsPlaying());

	p.Scrub(100.0f); // clamp high
	EXPECT_FLOAT_EQ(p.Frame(), 40.0f);
	p.Scrub(-5.0f); // clamp low
	EXPECT_FLOAT_EQ(p.Frame(), 10.0f);
}

TEST(AnimationPlayer, ZeroLengthClipIsStable)
{
	AnimationPlayer p;
	p.SetClip(5.0f, 5.0f, 30.0f);
	p.Play();
	p.Update(1.0f);
	EXPECT_FLOAT_EQ(p.Frame(), 5.0f);
	EXPECT_FLOAT_EQ(p.Normalized(), 0.0f);
}

TEST(AnimationPlayer, PausedDoesNotAdvance)
{
	AnimationPlayer p;
	p.SetClip(0.0f, 30.0f, 30.0f);
	p.Pause();
	p.Update(1.0f);
	EXPECT_FLOAT_EQ(p.Frame(), 0.0f);
}
