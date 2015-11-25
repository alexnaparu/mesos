/**
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License
*/

#include <list>
#include <map>
#include <set>
#include <string>

#include <stout/fs.hpp>
#include <stout/path.hpp>
#include <stout/uuid.hpp>

#include <stout/os/find.hpp>
#include <stout/os/getcwd.hpp>
#include <stout/os/glob.hpp>
#include <stout/os/mkdir.hpp>
#include <stout/os/ls.hpp>
#include <stout/os/read.hpp>
#include <stout/os/touch.hpp>
#include <stout/os/write.hpp>

#include <stout/tests/utils.hpp>

using std::list;
using std::set;
using std::string;


static hashset<string> listfiles(const string& directory)
{
  hashset<string> fileset;
  Try<std::list<std::string> > entries = os::ls(directory);
  if (entries.isSome()) {
    foreach (const string& entry, entries.get()) {
      fileset.insert(entry);
    }
  }
  return fileset;
}


class FsTest : public TemporaryDirectoryTest {};


TEST_F(FsTest, Find)
{
  const string testdir = path::join(os::getcwd(), UUID::random().toString());
  const string subdir = testdir + "/test1";
  ASSERT_SOME(os::mkdir(subdir)); // Create the directories.

  // Now write some files.
  const string file1 = testdir + "/file1.txt";
  const string file2 = subdir + "/file2.txt";
  const string file3 = subdir + "/file3.jpg";

  ASSERT_SOME(os::touch(file1));
  ASSERT_SOME(os::touch(file2));
  ASSERT_SOME(os::touch(file3));

  // Find "*.txt" files.
  Try<std::list<string> > result = os::find(testdir, ".txt");
  ASSERT_SOME(result);

  hashset<string> files;
  foreach (const string& file, result.get()) {
    files.insert(file);
  }

  ASSERT_EQ(2u, files.size());
  ASSERT_TRUE(files.contains(file1));
  ASSERT_TRUE(files.contains(file2));
}


TEST_F(FsTest, ReadWriteString)
{
  const string testfile  = path::join(os::getcwd(), UUID::random().toString());
  const string teststr = "line1\nline2";

  ASSERT_SOME(os::write(testfile, teststr));

  Try<string> readstr = os::read(testfile);

  ASSERT_SOME(readstr);
  EXPECT_EQ(teststr, readstr.get());
}


TEST_F(FsTest, Rmdir)
{
  const hashset<string> EMPTY;
  const string tmpdir = os::getcwd();

  hashset<string> expectedListing = EMPTY;
  EXPECT_EQ(expectedListing, listfiles(tmpdir));

  os::mkdir(tmpdir + "/a/b/c");
  os::mkdir(tmpdir + "/a/b/d");
  os::mkdir(tmpdir + "/e/f");

  expectedListing = EMPTY;
  expectedListing.insert("a");
  expectedListing.insert("e");
  EXPECT_EQ(expectedListing, listfiles(tmpdir));

  expectedListing = EMPTY;
  expectedListing.insert("b");
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/a"));

  expectedListing = EMPTY;
  expectedListing.insert("c");
  expectedListing.insert("d");
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/a/b"));

  expectedListing = EMPTY;
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/a/b/c"));
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/a/b/d"));

  expectedListing.insert("f");
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/e"));

  expectedListing = EMPTY;
  EXPECT_EQ(expectedListing, listfiles(tmpdir + "/e/f"));
}


TEST_F(FsTest, Touch)
{
  const string testfile  = path::join(os::getcwd(), UUID::random().toString());

  ASSERT_SOME(os::touch(testfile));
  ASSERT_TRUE(os::exists(testfile));
}

TEST_F(FsTest, Symlink)
{
  const string tempPath = os::getcwd();
  const string symlinkPath = path::join(tempPath, "/sym.link");
  const string symlinkTarget = path::join(tempPath, UUID::random().toString());

  // Create file
  ASSERT_SOME(os::touch(symlinkTarget))
      << "Failed to create file '" << symlinkTarget << "'";
  ASSERT_TRUE(os::exists(symlinkTarget));

  // Create symlink
  fs::symlink(symlinkTarget, symlinkPath);

  // Test symlink
  ASSERT_TRUE(os::stat::islink(symlinkPath));
}


TEST_F(FsTest, Glob)
{
  const string testdir = path::join(os::getcwd(), UUID::random().toString());
  ASSERT_SOME(os::mkdir(testdir)); // Create the directories.

  // Now write some files.
  const string file1 = testdir + "/file1.txt";
  const string file2 = testdir + "/file2.txt";
  const string file3 = testdir + "/file3.jpg";

  ASSERT_SOME(os::touch(file1));
  ASSERT_SOME(os::touch(file2));
  ASSERT_SOME(os::touch(file3));

  // Search all files in folder
  Try<std::list<std::string>> allFiles = os::glob(path::join(testdir, "*"));
  ASSERT_TRUE(allFiles.get().size() == 3);

  // Search .jpg files in folder
  Try<std::list<std::string>> jpgFiles = os::glob(path::join(testdir, "*.jpg"));
  ASSERT_TRUE(jpgFiles.get().size() == 1);

  // Search test*.txt files in folder
  Try<std::list<std::string>> testTxtFiles =
    os::glob(path::join(testdir, "*.txt"));

  ASSERT_TRUE(testTxtFiles.get().size() == 2);
}
