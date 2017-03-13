#include "memory.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <iostream>

#include "utils/random.h"
#include "utils/time.h"

namespace bess {

TEST(PhyMemTest, Phy2Virt) {
  int x = 0;  // &x is a valid address
  EXPECT_NE(Virt2PhyGeneric(&x), 0);
}

TEST(HugepageTest, BadSize) {
  // 4MB hugepages, which does not exist on x86_64
  ASSERT_DEATH(AllocHugepage(static_cast<Hugepage>(1 << 22)), "");
}

class HugepageTest : public ::testing::TestWithParam<Hugepage> {
 public:
  virtual void SetUp() override {
    ptr_ = AllocHugepage(GetParam());
    size_ = static_cast<size_t>(GetParam());
  }

  virtual void TearDown() override {
    FreeHugepage(ptr_);
  }

 protected:
  static const int kTestIterations = 100000;

  void *ptr_;
  size_t size_;
  Random rd_;
};

TEST_P(HugepageTest, BasicAlloc) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

	EXPECT_EQ(ptr_, Phy2Virt(Virt2Phy(ptr_)));
	EXPECT_EQ(Virt2Phy(ptr_), Virt2PhyGeneric(ptr_));
}

TEST_P(HugepageTest, Access) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  uint64_t *ptr = reinterpret_cast<uint64_t *>(ptr_);
  size_t num_elems = size_ / sizeof(*ptr);

  // WRITE
  for (size_t i = 0; i < num_elems; i++) {
    ptr[i] = i + 123456789;
  }

  // READ
  for (size_t i = 0; i < num_elems; i++) {
    EXPECT_EQ(ptr[i], i + 123456789);
  }
}

TEST_P(HugepageTest, AllZero) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  uint64_t *ptr = reinterpret_cast<uint64_t *>(ptr_);
  size_t num_elems = size_ / sizeof(*ptr);

  for (size_t i = 0; i < num_elems; i++) {
    EXPECT_EQ(ptr[i], 0);
  }
}

// The allocated page is physically contiguous?
TEST_P(HugepageTest, Contiguous) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  char *ptr = reinterpret_cast<char *>(ptr_);

  for (auto i = 0; i < kTestIterations; i++) {
    size_t offset = rd_.GetRange(size_);
    EXPECT_EQ(Virt2Phy(ptr) + offset, Virt2PhyGeneric(ptr + offset));
  }
}

TEST_P(HugepageTest, LeakFree) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  double start = get_cpu_time();

  do {
    // Already allocated, so free first
    FreeHugepage(ptr_);

    ptr_ = AllocHugepage(GetParam());
    EXPECT_NE(ptr_, nullptr);
  } while (get_cpu_time() - start < 0.5);  // 0.5 second for each page size
}

INSTANTIATE_TEST_CASE_P(PageSize, HugepageTest,
                        ::testing::Values(Hugepage::kSize4KB,
                                          Hugepage::kSize2MB,
                                          Hugepage::kSize1GB));

}  // namespace bess
