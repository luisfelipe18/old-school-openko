// Headless asset-pipeline smoke test (docs/PORT_POSIX_PLAN.md, phase 4/5
// acceptance): a collision mesh in the game's real .n3vmesh layout is
// written, loaded back through the engine's own loader (CN3VMesh ->
// CN3BaseFileAccess -> FileReader) and rendered through the Null RHI device.

#include <gtest/gtest.h>

#include <FileIO/FileReader.h>
#include <FileIO/FileWriter.h>
#include <N3Base/N3VMesh.h>
#include <N3Base/RHI/RHIDeviceNull.h>

#include <atomic>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);

	return fs::temp_directory_path()
		   / ("VMeshTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++)
			  + ".n3vmesh");
}

// On-disk layout written by CN3VMesh::Save (name-length + name from
// CN3BaseFileAccess, then vertex count/vertices, index count/indices).
void WriteSyntheticVMesh(const fs::path& path)
{
	FileWriter file;
	ASSERT_TRUE(file.Create(path));

	const int nameLength = 0;
	file.Write(&nameLength, 4);

	// A unit quad out of 4 vertices / 2 triangles.
	const __Vector3 vertices[4] = {
		{0.0f, 0.0f, 0.0f},
		{1.0f, 0.0f, 0.0f},
		{1.0f, 1.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
	};
	const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

	const int vertexCount = 4;
	file.Write(&vertexCount, 4);
	file.Write(vertices, sizeof(vertices));

	const int indexCount = 6;
	file.Write(&indexCount, 4);
	file.Write(indices, sizeof(indices));

	file.Close();
}
} // namespace

TEST(VMeshHeadlessTest, LoadsAndRendersThroughTheNullDevice)
{
	const fs::path path = MakeTempPath();
	WriteSyntheticVMesh(path);

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CN3VMesh mesh;
		FileReader file;
		ASSERT_TRUE(file.OpenExisting(path));
		EXPECT_TRUE(mesh.Load(file));

		EXPECT_EQ(mesh.VertexCount(), 4);
		EXPECT_EQ(mesh.IndexCount(), 6);
		EXPECT_GT(mesh.Radius(), 0.0f);

		// Render through the RHI: with the Null backend this must produce
		// draw calls but no crashes and no GPU.
		mesh.Render(0xFFFF0000);
		EXPECT_GT(device.DrawCallCount(), 0);
	}

	CN3Base::RHIDeviceSet(nullptr);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(VMeshHeadlessTest, CaseInsensitivePathStillLoads)
{
	const fs::path path = MakeTempPath();
	WriteSyntheticVMesh(path);

	// Load through a lowercased version of the path, exercising the
	// PathResolver integration (the engine lowercases all asset paths).
	std::string lowered = path.string();
	for (char& ch : lowered)
	{
		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'A';
	}

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CN3VMesh mesh;
		FileReader file;
#ifdef _WIN32
		const bool opened = file.OpenExisting(path); // resolver is POSIX-only
#else
		const bool opened = file.OpenExisting(lowered);
#endif
		ASSERT_TRUE(opened);
		EXPECT_TRUE(mesh.Load(file));
		EXPECT_EQ(mesh.VertexCount(), 4);
	}

	CN3Base::RHIDeviceSet(nullptr);

	std::error_code ec;
	fs::remove(path, ec);
}
