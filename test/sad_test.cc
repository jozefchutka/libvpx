/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <string.h>
#include <limits.h>
#include <stdio.h>

#include "./vpx_config.h"
#if CONFIG_VP8_ENCODER
#include "./vp8_rtcd.h"
#endif
#if CONFIG_VP9_ENCODER
#include "./vp9_rtcd.h"
#endif
#include "vpx_mem/vpx_mem.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "vpx/vpx_codec.h"


#if CONFIG_VP8_ENCODER
typedef unsigned int (*SadMxNFunc)(const unsigned char *source_ptr,
                                   int source_stride,
                                   const unsigned char *reference_ptr,
                                   int reference_stride,
                                   unsigned int max_sad);
typedef std::tr1::tuple<int, int, SadMxNFunc, int> SadMxNParam;
#endif
#if CONFIG_VP9_ENCODER
typedef unsigned int (*SadMxNVp9Func)(const unsigned char *source_ptr,
                                      int source_stride,
                                      const unsigned char *reference_ptr,
                                      int reference_stride);
typedef std::tr1::tuple<int, int, SadMxNVp9Func, int> SadMxNVp9Param;
typedef uint32_t (*SadMxNAvgVp9Func)(const uint8_t *source_ptr,
                                     int source_stride,
                                     const uint8_t *reference_ptr,
                                     int reference_stride,
                                     const uint8_t *second_pred);
typedef std::tr1::tuple<int, int, SadMxNAvgVp9Func, int> SadMxNAvgVp9Param;
#endif

typedef void (*SadMxNx4Func)(const uint8_t *src_ptr,
                             int src_stride,
                             const uint8_t *const ref_ptr[],
                             int ref_stride,
                             uint32_t *sad_array);
typedef std::tr1::tuple<int, int, SadMxNx4Func, int> SadMxNx4Param;

using libvpx_test::ACMRandom;

namespace {
class SADTestBase : public ::testing::Test {
 public:
  SADTestBase(int width, int height, int bit_depth) :
      width_(width), height_(height), bd_(bit_depth) {}

  static void SetUpTestCase() {
#if CONFIG_VP9_HIGHBITDEPTH
    source_data8_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, kDataBlockSize));
    reference_data8_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, kDataBufferSize));
    second_pred8_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, 64*64));
    source_data16_ = reinterpret_cast<uint16_t*>(
        vpx_memalign(kDataAlignment, kDataBlockSize*sizeof(uint16_t)));
    reference_data16_ = reinterpret_cast<uint16_t*>(
        vpx_memalign(kDataAlignment, kDataBufferSize*sizeof(uint16_t)));
    second_pred16_ = reinterpret_cast<uint16_t*>(
        vpx_memalign(kDataAlignment, 64*64*sizeof(uint16_t)));
#else
    source_data_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, kDataBlockSize));
    reference_data_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, kDataBufferSize));
    second_pred_ = reinterpret_cast<uint8_t*>(
        vpx_memalign(kDataAlignment, 64*64));
#endif
  }

  static void TearDownTestCase() {
#if CONFIG_VP9_HIGHBITDEPTH
    vpx_free(source_data8_);
    source_data8_ = NULL;
    vpx_free(reference_data8_);
    reference_data8_ = NULL;
    vpx_free(second_pred8_);
    second_pred8_ = NULL;
    vpx_free(source_data16_);
    source_data16_ = NULL;
    vpx_free(reference_data16_);
    reference_data16_ = NULL;
    vpx_free(second_pred16_);
    second_pred16_ = NULL;
#else
    vpx_free(source_data_);
    source_data_ = NULL;
    vpx_free(reference_data_);
    reference_data_ = NULL;
    vpx_free(second_pred_);
    second_pred_ = NULL;
#endif
  }

  virtual void TearDown() {
    libvpx_test::ClearSystemState();
  }

 protected:
  // Handle blocks up to 4 blocks 64x64 with stride up to 128
  static const int kDataAlignment = 16;
  static const int kDataBlockSize = 64 * 128;
  static const int kDataBufferSize = 4 * kDataBlockSize;

  virtual void SetUp() {
#if CONFIG_VP9_HIGHBITDEPTH
    if (bd_ == -1) {
      use_high_bit_depth_ = false;
      bit_depth_ = VPX_BITS_8;
      source_data_ = source_data8_;
      reference_data_ = reference_data8_;
      second_pred_ = second_pred8_;
    } else {
      use_high_bit_depth_ = true;
      bit_depth_ = static_cast<vpx_bit_depth_t>(bd_);
      source_data_ = CONVERT_TO_BYTEPTR(source_data16_);
      reference_data_ = CONVERT_TO_BYTEPTR(reference_data16_);
      second_pred_ = CONVERT_TO_BYTEPTR(second_pred16_);
    }
#endif
    mask_ = (1 << bit_depth_) - 1;
    source_stride_ = (width_ + 31) & ~31;
    reference_stride_ = width_ * 2;
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  virtual uint8_t *GetReference(int block_idx) {
#if CONFIG_VP9_HIGHBITDEPTH
    if (!use_high_bit_depth_) {
      return reference_data_ + block_idx * kDataBlockSize;
    } else {
      return CONVERT_TO_BYTEPTR(CONVERT_TO_SHORTPTR(reference_data_) +
                                block_idx * kDataBlockSize);
    }
#else
    return reference_data_ + block_idx * kDataBlockSize;
#endif
  }

  // Sum of Absolute Differences. Given two blocks, calculate the absolute
  // difference between two pixels in the same relative location; accumulate.
  unsigned int ReferenceSAD(unsigned int max_sad, int block_idx) {
    unsigned int sad = 0;
#if CONFIG_VP9_HIGHBITDEPTH
      const uint8_t *const reference8 = GetReference(block_idx);
      const uint8_t *const source8 = source_data_;
      const uint16_t *const reference16 =
          CONVERT_TO_SHORTPTR(GetReference(block_idx));
      const uint16_t *const source16 = CONVERT_TO_SHORTPTR(source_data_);
#else
    const uint8_t *const reference = GetReference(block_idx);
    const uint8_t *const source = source_data_;
#endif
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (!use_high_bit_depth_) {
          sad +=
              abs(source8[h * source_stride_ + w] -
                  reference8[h * reference_stride_ + w]);
        } else {
          sad +=
              abs(source16[h * source_stride_ + w] -
                  reference16[h * reference_stride_ + w]);
        }
#else
        sad +=
            abs(source[h * source_stride_ + w] -
                reference[h * reference_stride_ + w]);
#endif
      }
      if (sad > max_sad) {
        break;
      }
    }
    return sad;
  }

  // Sum of Absolute Differences Average. Given two blocks, and a prediction
  // calculate the absolute difference between one pixel and average of the
  // corresponding and predicted pixels; accumulate.
  unsigned int ReferenceSADavg(unsigned int max_sad, int block_idx) {
    unsigned int sad = 0;
#if CONFIG_VP9_HIGHBITDEPTH
      const uint8_t *const reference8 = GetReference(block_idx);
      const uint8_t *const source8 = source_data_;
      const uint8_t *const second_pred8 = second_pred_;
      const uint16_t *const reference16 =
          CONVERT_TO_SHORTPTR(GetReference(block_idx));
      const uint16_t *const source16 = CONVERT_TO_SHORTPTR(source_data_);
      const uint16_t *const second_pred16 = CONVERT_TO_SHORTPTR(second_pred_);
#else
    const uint8_t *const reference = GetReference(block_idx);
    const uint8_t *const source = source_data_;
    const uint8_t *const second_pred = second_pred_;
#endif
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (!use_high_bit_depth_) {
          const int tmp = second_pred8[h * width_ + w] +
              reference8[h * reference_stride_ + w];
          const uint8_t comp_pred = ROUND_POWER_OF_TWO(tmp, 1);
          sad += abs(source8[h * source_stride_ + w] - comp_pred);
        } else {
          const int tmp = second_pred16[h * width_ + w] +
              reference16[h * reference_stride_ + w];
          const uint16_t comp_pred = ROUND_POWER_OF_TWO(tmp, 1);
          sad += abs(source16[h * source_stride_ + w] - comp_pred);
        }
#else
        const int tmp = second_pred[h * width_ + w] +
            reference[h * reference_stride_ + w];
        const uint8_t comp_pred = ROUND_POWER_OF_TWO(tmp, 1);
        sad += abs(source[h * source_stride_ + w] - comp_pred);
#endif
      }
      if (sad > max_sad) {
        break;
      }
    }
    return sad;
  }

  void FillConstant(uint8_t *data, int stride, uint16_t fill_constant) {
#if CONFIG_VP9_HIGHBITDEPTH
    uint8_t *data8 = data;
    uint16_t *data16 = CONVERT_TO_SHORTPTR(data);
#endif
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (!use_high_bit_depth_) {
          data8[h * stride + w] = static_cast<uint8_t>(fill_constant);
        } else {
          data16[h * stride + w] = fill_constant;
        }
#else
        data[h * stride + w] = static_cast<uint8_t>(fill_constant);
#endif
      }
    }
  }

  void FillRandom(uint8_t *data, int stride) {
#if CONFIG_VP9_HIGHBITDEPTH
    uint8_t *data8 = data;
    uint16_t *data16 = CONVERT_TO_SHORTPTR(data);
#endif
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (!use_high_bit_depth_) {
          data8[h * stride + w] = rnd_.Rand8();
        } else {
          data16[h * stride + w] = rnd_.Rand16() & mask_;
        }
#else
        data[h * stride + w] = rnd_.Rand8();
#endif
      }
    }
  }

  int width_, height_, mask_, bd_;
  vpx_bit_depth_t bit_depth_;
  static uint8_t *source_data_;
  static uint8_t *reference_data_;
  static uint8_t *second_pred_;
  int source_stride_;
#if CONFIG_VP9_HIGHBITDEPTH
  bool use_high_bit_depth_;
  static uint8_t *source_data8_;
  static uint8_t *reference_data8_;
  static uint8_t *second_pred8_;
  static uint16_t *source_data16_;
  static uint16_t *reference_data16_;
  static uint16_t *second_pred16_;
#endif
  int reference_stride_;

  ACMRandom rnd_;
};

class SADx4Test
    : public SADTestBase,
      public ::testing::WithParamInterface<SadMxNx4Param> {
 public:
  SADx4Test() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  void SADs(unsigned int *results) {
    const uint8_t *refs[] = {GetReference(0), GetReference(1),
                             GetReference(2), GetReference(3)};

    ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(source_data_, source_stride_,
                                          refs, reference_stride_,
                                          results));
  }

  void CheckSADs() {
    unsigned int reference_sad, exp_sad[4];

    SADs(exp_sad);
    for (int block = 0; block < 4; ++block) {
      reference_sad = ReferenceSAD(UINT_MAX, block);

      EXPECT_EQ(reference_sad, exp_sad[block]) << "block " << block;
    }
  }
};

#if CONFIG_VP8_ENCODER
class SADTest
    : public SADTestBase,
      public ::testing::WithParamInterface<SadMxNParam> {
 public:
  SADTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD(unsigned int max_sad, int block_idx) {
    unsigned int ret;
    const uint8_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_,
                                                max_sad));
    return ret;
  }

  void CheckSAD(unsigned int max_sad) {
    const unsigned int reference_sad = ReferenceSAD(max_sad, 0);
    const unsigned int exp_sad = SAD(max_sad, 0);

    if (reference_sad <= max_sad) {
      ASSERT_EQ(exp_sad, reference_sad);
    } else {
      // Alternative implementations are not required to check max_sad
      ASSERT_GE(exp_sad, reference_sad);
    }
  }
};
#endif  // CONFIG_VP8_ENCODER

#if CONFIG_VP9_ENCODER
class SADVP9Test
    : public SADTestBase,
      public ::testing::WithParamInterface<SadMxNVp9Param> {
 public:
  SADVP9Test() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD(int block_idx) {
    unsigned int ret;
    const uint8_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSAD(UINT_MAX, 0);
    const unsigned int exp_sad = SAD(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }
};

class SADavgVP9Test
    : public SADTestBase,
      public ::testing::WithParamInterface<SadMxNAvgVp9Param> {
 public:
  SADavgVP9Test() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD_avg(int block_idx) {
    unsigned int ret;
    const uint8_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_,
                                                second_pred_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSADavg(UINT_MAX, 0);
    const unsigned int exp_sad = SAD_avg(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }
};
#endif  // CONFIG_VP9_ENCODER

uint8_t *SADTestBase::source_data_ = NULL;
uint8_t *SADTestBase::reference_data_ = NULL;
uint8_t *SADTestBase::second_pred_ = NULL;
#if CONFIG_VP9_ENCODER && CONFIG_VP9_HIGHBITDEPTH
uint8_t *SADTestBase::source_data8_ = NULL;
uint8_t *SADTestBase::reference_data8_ = NULL;
uint8_t *SADTestBase::second_pred8_ = NULL;
uint16_t *SADTestBase::source_data16_ = NULL;
uint16_t *SADTestBase::reference_data16_ = NULL;
uint16_t *SADTestBase::second_pred16_ = NULL;
#endif

#if CONFIG_VP8_ENCODER
TEST_P(SADTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD(UINT_MAX);
}

TEST_P(SADTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD(UINT_MAX);
}

TEST_P(SADTest, ShortRef) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD(UINT_MAX);
  reference_stride_ = tmp_stride;
}

TEST_P(SADTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD(UINT_MAX);
  reference_stride_ = tmp_stride;
}

TEST_P(SADTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD(UINT_MAX);
  source_stride_ = tmp_stride;
}

TEST_P(SADTest, MaxSAD) {
  // Verify that, when max_sad is set, the implementation does not return a
  // value lower than the reference.
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD(128);
}
#endif  // CONFIG_VP8_ENCODER

#if CONFIG_VP9_ENCODER
TEST_P(SADVP9Test, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD();
}

TEST_P(SADVP9Test, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD();
}

TEST_P(SADVP9Test, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADVP9Test, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADVP9Test, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  source_stride_ = tmp_stride;
}

TEST_P(SADavgVP9Test, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}
TEST_P(SADavgVP9Test, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}

TEST_P(SADavgVP9Test, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADavgVP9Test, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADavgVP9Test, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  source_stride_ = tmp_stride;
}
#endif  // CONFIG_VP9_ENCODER

TEST_P(SADx4Test, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(GetReference(0), reference_stride_, mask_);
  FillConstant(GetReference(1), reference_stride_, mask_);
  FillConstant(GetReference(2), reference_stride_, mask_);
  FillConstant(GetReference(3), reference_stride_, mask_);
  CheckSADs();
}

TEST_P(SADx4Test, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(GetReference(0), reference_stride_, 0);
  FillConstant(GetReference(1), reference_stride_, 0);
  FillConstant(GetReference(2), reference_stride_, 0);
  FillConstant(GetReference(3), reference_stride_, 0);
  CheckSADs();
}

TEST_P(SADx4Test, ShortRef) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4Test, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4Test, ShortSrc) {
  int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  source_stride_ = tmp_stride;
}

TEST_P(SADx4Test, SrcAlignedByWidth) {
  uint8_t * tmp_source_data = source_data_;
  source_data_ += width_;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  source_data_ = tmp_source_data;
}

using std::tr1::make_tuple;

//------------------------------------------------------------------------------
// C functions
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_c = vp8_sad16x16_c;
const SadMxNFunc sad_8x16_c = vp8_sad8x16_c;
const SadMxNFunc sad_16x8_c = vp8_sad16x8_c;
const SadMxNFunc sad_8x8_c = vp8_sad8x8_c;
const SadMxNFunc sad_4x4_c = vp8_sad4x4_c;
const SadMxNParam c_tests[] = {
  make_tuple(16, 16, sad_16x16_c, -1),
  make_tuple(8, 16, sad_8x16_c, -1),
  make_tuple(16, 8, sad_16x8_c, -1),
  make_tuple(8, 8, sad_8x8_c, -1),
  make_tuple(4, 4, sad_4x4_c, -1),
};
INSTANTIATE_TEST_CASE_P(C, SADTest, ::testing::ValuesIn(c_tests));
#endif  // CONFIG_VP8_ENCODER

#if CONFIG_VP9_ENCODER
const SadMxNVp9Func sad_64x64_c_vp9 = vp9_sad64x64_c;
const SadMxNVp9Func sad_32x32_c_vp9 = vp9_sad32x32_c;
const SadMxNVp9Func sad_16x16_c_vp9 = vp9_sad16x16_c;
const SadMxNVp9Func sad_8x16_c_vp9 = vp9_sad8x16_c;
const SadMxNVp9Func sad_16x8_c_vp9 = vp9_sad16x8_c;
const SadMxNVp9Func sad_8x8_c_vp9 = vp9_sad8x8_c;
const SadMxNVp9Func sad_8x4_c_vp9 = vp9_sad8x4_c;
const SadMxNVp9Func sad_4x8_c_vp9 = vp9_sad4x8_c;
const SadMxNVp9Func sad_4x4_c_vp9 = vp9_sad4x4_c;
const SadMxNVp9Param c_vp9_tests[] = {
  make_tuple(64, 64, sad_64x64_c_vp9, -1),
  make_tuple(32, 32, sad_32x32_c_vp9, -1),
  make_tuple(16, 16, sad_16x16_c_vp9, -1),
  make_tuple(8, 16, sad_8x16_c_vp9, -1),
  make_tuple(16, 8, sad_16x8_c_vp9, -1),
  make_tuple(8, 8, sad_8x8_c_vp9, -1),
  make_tuple(8, 4, sad_8x4_c_vp9, -1),
  make_tuple(4, 8, sad_4x8_c_vp9, -1),
  make_tuple(4, 4, sad_4x4_c_vp9, -1),
};
INSTANTIATE_TEST_CASE_P(C, SADVP9Test, ::testing::ValuesIn(c_vp9_tests));

const SadMxNx4Func sad_64x64x4d_c = vp9_sad64x64x4d_c;
const SadMxNx4Func sad_64x32x4d_c = vp9_sad64x32x4d_c;
const SadMxNx4Func sad_32x64x4d_c = vp9_sad32x64x4d_c;
const SadMxNx4Func sad_32x32x4d_c = vp9_sad32x32x4d_c;
const SadMxNx4Func sad_32x16x4d_c = vp9_sad32x16x4d_c;
const SadMxNx4Func sad_16x32x4d_c = vp9_sad16x32x4d_c;
const SadMxNx4Func sad_16x16x4d_c = vp9_sad16x16x4d_c;
const SadMxNx4Func sad_16x8x4d_c = vp9_sad16x8x4d_c;
const SadMxNx4Func sad_8x16x4d_c = vp9_sad8x16x4d_c;
const SadMxNx4Func sad_8x8x4d_c = vp9_sad8x8x4d_c;
const SadMxNx4Func sad_8x4x4d_c = vp9_sad8x4x4d_c;
const SadMxNx4Func sad_4x8x4d_c = vp9_sad4x8x4d_c;
const SadMxNx4Func sad_4x4x4d_c = vp9_sad4x4x4d_c;
INSTANTIATE_TEST_CASE_P(C, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, sad_64x64x4d_c, -1),
                        make_tuple(64, 32, sad_64x32x4d_c, -1),
                        make_tuple(32, 64, sad_32x64x4d_c, -1),
                        make_tuple(32, 32, sad_32x32x4d_c, -1),
                        make_tuple(32, 16, sad_32x16x4d_c, -1),
                        make_tuple(16, 32, sad_16x32x4d_c, -1),
                        make_tuple(16, 16, sad_16x16x4d_c, -1),
                        make_tuple(16, 8, sad_16x8x4d_c, -1),
                        make_tuple(8, 16, sad_8x16x4d_c, -1),
                        make_tuple(8, 8, sad_8x8x4d_c, -1),
                        make_tuple(8, 4, sad_8x4x4d_c, -1),
                        make_tuple(4, 8, sad_4x8x4d_c, -1),
                        make_tuple(4, 4, sad_4x4x4d_c, -1)));

#if CONFIG_VP9_HIGHBITDEPTH
const SadMxNVp9Func highbd_sad_64x64_c_vp9 = vp9_highbd_sad64x64_c;
const SadMxNVp9Func highbd_sad_32x32_c_vp9 = vp9_highbd_sad32x32_c;
const SadMxNVp9Func highbd_sad_16x16_c_vp9 = vp9_highbd_sad16x16_c;
const SadMxNVp9Func highbd_sad_8x16_c_vp9 = vp9_highbd_sad8x16_c;
const SadMxNVp9Func highbd_sad_16x8_c_vp9 = vp9_highbd_sad16x8_c;
const SadMxNVp9Func highbd_sad_8x8_c_vp9 = vp9_highbd_sad8x8_c;
const SadMxNVp9Func highbd_sad_8x4_c_vp9 = vp9_highbd_sad8x4_c;
const SadMxNVp9Func highbd_sad_4x8_c_vp9 = vp9_highbd_sad4x8_c;
const SadMxNVp9Func highbd_sad_4x4_c_vp9 = vp9_highbd_sad4x4_c;
const SadMxNVp9Param c_vp9_highbd_8_tests[] = {
  make_tuple(64, 64, highbd_sad_64x64_c_vp9, 8),
  make_tuple(32, 32, highbd_sad_32x32_c_vp9, 8),
  make_tuple(16, 16, highbd_sad_16x16_c_vp9, 8),
  make_tuple(8, 16, highbd_sad_8x16_c_vp9, 8),
  make_tuple(16, 8, highbd_sad_16x8_c_vp9, 8),
  make_tuple(8, 8, highbd_sad_8x8_c_vp9, 8),
  make_tuple(8, 4, highbd_sad_8x4_c_vp9, 8),
  make_tuple(4, 8, highbd_sad_4x8_c_vp9, 8),
  make_tuple(4, 4, highbd_sad_4x4_c_vp9, 8),
};
INSTANTIATE_TEST_CASE_P(C_8, SADVP9Test,
                        ::testing::ValuesIn(c_vp9_highbd_8_tests));

const SadMxNVp9Param c_vp9_highbd_10_tests[] = {
  make_tuple(64, 64, highbd_sad_64x64_c_vp9, 10),
  make_tuple(32, 32, highbd_sad_32x32_c_vp9, 10),
  make_tuple(16, 16, highbd_sad_16x16_c_vp9, 10),
  make_tuple(8, 16, highbd_sad_8x16_c_vp9, 10),
  make_tuple(16, 8, highbd_sad_16x8_c_vp9, 10),
  make_tuple(8, 8, highbd_sad_8x8_c_vp9, 10),
  make_tuple(8, 4, highbd_sad_8x4_c_vp9, 10),
  make_tuple(4, 8, highbd_sad_4x8_c_vp9, 10),
  make_tuple(4, 4, highbd_sad_4x4_c_vp9, 10),
};
INSTANTIATE_TEST_CASE_P(C_10, SADVP9Test,
                        ::testing::ValuesIn(c_vp9_highbd_10_tests));

const SadMxNVp9Param c_vp9_highbd_12_tests[] = {
  make_tuple(64, 64, highbd_sad_64x64_c_vp9, 12),
  make_tuple(32, 32, highbd_sad_32x32_c_vp9, 12),
  make_tuple(16, 16, highbd_sad_16x16_c_vp9, 12),
  make_tuple(8, 16, highbd_sad_8x16_c_vp9, 12),
  make_tuple(16, 8, highbd_sad_16x8_c_vp9, 12),
  make_tuple(8, 8, highbd_sad_8x8_c_vp9, 12),
  make_tuple(8, 4, highbd_sad_8x4_c_vp9, 12),
  make_tuple(4, 8, highbd_sad_4x8_c_vp9, 12),
  make_tuple(4, 4, highbd_sad_4x4_c_vp9, 12),
};
INSTANTIATE_TEST_CASE_P(C_12, SADVP9Test,
                        ::testing::ValuesIn(c_vp9_highbd_12_tests));

const SadMxNAvgVp9Func highbd_sad8x4_avg_c_vp9 = vp9_highbd_sad8x4_avg_c;
const SadMxNAvgVp9Func highbd_sad8x8_avg_c_vp9 = vp9_highbd_sad8x8_avg_c;
const SadMxNAvgVp9Func highbd_sad8x16_avg_c_vp9 = vp9_highbd_sad8x16_avg_c;
const SadMxNAvgVp9Func highbd_sad16x8_avg_c_vp9 = vp9_highbd_sad16x8_avg_c;
const SadMxNAvgVp9Func highbd_sad16x16_avg_c_vp9 = vp9_highbd_sad16x16_avg_c;
const SadMxNAvgVp9Func highbd_sad16x32_avg_c_vp9 = vp9_highbd_sad16x32_avg_c;
const SadMxNAvgVp9Func highbd_sad32x16_avg_c_vp9 = vp9_highbd_sad32x16_avg_c;
const SadMxNAvgVp9Func highbd_sad32x32_avg_c_vp9 = vp9_highbd_sad32x32_avg_c;
const SadMxNAvgVp9Func highbd_sad32x64_avg_c_vp9 = vp9_highbd_sad32x64_avg_c;
const SadMxNAvgVp9Func highbd_sad64x32_avg_c_vp9 = vp9_highbd_sad64x32_avg_c;
const SadMxNAvgVp9Func highbd_sad64x64_avg_c_vp9 = vp9_highbd_sad64x64_avg_c;
SadMxNAvgVp9Param avg_c_vp9_highbd_8_tests[] = {
  make_tuple(8, 4, highbd_sad8x4_avg_c_vp9, 8),
  make_tuple(8, 8, highbd_sad8x8_avg_c_vp9, 8),
  make_tuple(8, 16, highbd_sad8x16_avg_c_vp9, 8),
  make_tuple(16, 8, highbd_sad16x8_avg_c_vp9, 8),
  make_tuple(16, 16, highbd_sad16x16_avg_c_vp9, 8),
  make_tuple(16, 32, highbd_sad16x32_avg_c_vp9, 8),
  make_tuple(32, 16, highbd_sad32x16_avg_c_vp9, 8),
  make_tuple(32, 32, highbd_sad32x32_avg_c_vp9, 8),
  make_tuple(32, 64, highbd_sad32x64_avg_c_vp9, 8),
  make_tuple(64, 32, highbd_sad64x32_avg_c_vp9, 8),
  make_tuple(64, 64, highbd_sad64x64_avg_c_vp9, 8)};
INSTANTIATE_TEST_CASE_P(C_8, SADavgVP9Test,
                        ::testing::ValuesIn(avg_c_vp9_highbd_8_tests));

SadMxNAvgVp9Param avg_c_vp9_highbd_10_tests[] = {
  make_tuple(8, 4, highbd_sad8x4_avg_c_vp9, 10),
  make_tuple(8, 8, highbd_sad8x8_avg_c_vp9, 10),
  make_tuple(8, 16, highbd_sad8x16_avg_c_vp9, 10),
  make_tuple(16, 8, highbd_sad16x8_avg_c_vp9, 10),
  make_tuple(16, 16, highbd_sad16x16_avg_c_vp9, 10),
  make_tuple(16, 32, highbd_sad16x32_avg_c_vp9, 10),
  make_tuple(32, 16, highbd_sad32x16_avg_c_vp9, 10),
  make_tuple(32, 32, highbd_sad32x32_avg_c_vp9, 10),
  make_tuple(32, 64, highbd_sad32x64_avg_c_vp9, 10),
  make_tuple(64, 32, highbd_sad64x32_avg_c_vp9, 10),
  make_tuple(64, 64, highbd_sad64x64_avg_c_vp9, 10)};
INSTANTIATE_TEST_CASE_P(C_10, SADavgVP9Test,
                        ::testing::ValuesIn(avg_c_vp9_highbd_10_tests));

SadMxNAvgVp9Param avg_c_vp9_highbd_12_tests[] = {
  make_tuple(8, 4, highbd_sad8x4_avg_c_vp9, 12),
  make_tuple(8, 8, highbd_sad8x8_avg_c_vp9, 12),
  make_tuple(8, 16, highbd_sad8x16_avg_c_vp9, 12),
  make_tuple(16, 8, highbd_sad16x8_avg_c_vp9, 12),
  make_tuple(16, 16, highbd_sad16x16_avg_c_vp9, 12),
  make_tuple(16, 32, highbd_sad16x32_avg_c_vp9, 12),
  make_tuple(32, 16, highbd_sad32x16_avg_c_vp9, 12),
  make_tuple(32, 32, highbd_sad32x32_avg_c_vp9, 12),
  make_tuple(32, 64, highbd_sad32x64_avg_c_vp9, 12),
  make_tuple(64, 32, highbd_sad64x32_avg_c_vp9, 12),
  make_tuple(64, 64, highbd_sad64x64_avg_c_vp9, 12)};
INSTANTIATE_TEST_CASE_P(C_12, SADavgVP9Test,
                        ::testing::ValuesIn(avg_c_vp9_highbd_12_tests));

const SadMxNx4Func highbd_sad_64x64x4d_c = vp9_highbd_sad64x64x4d_c;
const SadMxNx4Func highbd_sad_64x32x4d_c = vp9_highbd_sad64x32x4d_c;
const SadMxNx4Func highbd_sad_32x64x4d_c = vp9_highbd_sad32x64x4d_c;
const SadMxNx4Func highbd_sad_32x32x4d_c = vp9_highbd_sad32x32x4d_c;
const SadMxNx4Func highbd_sad_32x16x4d_c = vp9_highbd_sad32x16x4d_c;
const SadMxNx4Func highbd_sad_16x32x4d_c = vp9_highbd_sad16x32x4d_c;
const SadMxNx4Func highbd_sad_16x16x4d_c = vp9_highbd_sad16x16x4d_c;
const SadMxNx4Func highbd_sad_16x8x4d_c  = vp9_highbd_sad16x8x4d_c;
const SadMxNx4Func highbd_sad_8x16x4d_c  = vp9_highbd_sad8x16x4d_c;
const SadMxNx4Func highbd_sad_8x8x4d_c   = vp9_highbd_sad8x8x4d_c;
const SadMxNx4Func highbd_sad_8x4x4d_c   = vp9_highbd_sad8x4x4d_c;
const SadMxNx4Func highbd_sad_4x8x4d_c   = vp9_highbd_sad4x8x4d_c;
const SadMxNx4Func highbd_sad_4x4x4d_c   = vp9_highbd_sad4x4x4d_c;
INSTANTIATE_TEST_CASE_P(C_8, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, highbd_sad_64x64x4d_c, 8),
                        make_tuple(64, 32, highbd_sad_64x32x4d_c, 8),
                        make_tuple(32, 64, highbd_sad_32x64x4d_c, 8),
                        make_tuple(32, 32, highbd_sad_32x32x4d_c, 8),
                        make_tuple(32, 16, highbd_sad_32x16x4d_c, 8),
                        make_tuple(16, 32, highbd_sad_16x32x4d_c, 8),
                        make_tuple(16, 16, highbd_sad_16x16x4d_c, 8),
                        make_tuple(16, 8,  highbd_sad_16x8x4d_c,  8),
                        make_tuple(8,  16, highbd_sad_8x16x4d_c,  8),
                        make_tuple(8,  8,  highbd_sad_8x8x4d_c,   8),
                        make_tuple(8,  4,  highbd_sad_8x4x4d_c,   8),
                        make_tuple(4,  8,  highbd_sad_4x8x4d_c,   8),
                        make_tuple(4,  4,  highbd_sad_4x4x4d_c,   8)));

INSTANTIATE_TEST_CASE_P(C_10, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, highbd_sad_64x64x4d_c, 10),
                        make_tuple(64, 32, highbd_sad_64x32x4d_c, 10),
                        make_tuple(32, 64, highbd_sad_32x64x4d_c, 10),
                        make_tuple(32, 32, highbd_sad_32x32x4d_c, 10),
                        make_tuple(32, 16, highbd_sad_32x16x4d_c, 10),
                        make_tuple(16, 32, highbd_sad_16x32x4d_c, 10),
                        make_tuple(16, 16, highbd_sad_16x16x4d_c, 10),
                        make_tuple(16, 8,  highbd_sad_16x8x4d_c,  10),
                        make_tuple(8,  16, highbd_sad_8x16x4d_c,  10),
                        make_tuple(8,  8,  highbd_sad_8x8x4d_c,   10),
                        make_tuple(8,  4,  highbd_sad_8x4x4d_c,   10),
                        make_tuple(4,  8,  highbd_sad_4x8x4d_c,   10),
                        make_tuple(4,  4,  highbd_sad_4x4x4d_c,   10)));

INSTANTIATE_TEST_CASE_P(C_12, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, highbd_sad_64x64x4d_c, 12),
                        make_tuple(64, 32, highbd_sad_64x32x4d_c, 12),
                        make_tuple(32, 64, highbd_sad_32x64x4d_c, 12),
                        make_tuple(32, 32, highbd_sad_32x32x4d_c, 12),
                        make_tuple(32, 16, highbd_sad_32x16x4d_c, 12),
                        make_tuple(16, 32, highbd_sad_16x32x4d_c, 12),
                        make_tuple(16, 16, highbd_sad_16x16x4d_c, 12),
                        make_tuple(16, 8,  highbd_sad_16x8x4d_c,  12),
                        make_tuple(8,  16, highbd_sad_8x16x4d_c,  12),
                        make_tuple(8,  8,  highbd_sad_8x8x4d_c,   12),
                        make_tuple(8,  4,  highbd_sad_8x4x4d_c,   12),
                        make_tuple(4,  8,  highbd_sad_4x8x4d_c,   12),
                        make_tuple(4,  4,  highbd_sad_4x4x4d_c,   12)));
#endif  // CONFIG_VP9_HIGHBITDEPTH
#endif  // CONFIG_VP9_ENCODER

//------------------------------------------------------------------------------
// ARM functions
#if HAVE_MEDIA
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_armv6 = vp8_sad16x16_armv6;
INSTANTIATE_TEST_CASE_P(MEDIA, SADTest, ::testing::Values(
                        make_tuple(16, 16, sad_16x16_armv6, -1)));
#endif  // CONFIG_VP8_ENCODER
#endif  // HAVE_MEDIA

#if HAVE_NEON
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_neon = vp8_sad16x16_neon;
const SadMxNFunc sad_8x16_neon = vp8_sad8x16_neon;
const SadMxNFunc sad_16x8_neon = vp8_sad16x8_neon;
const SadMxNFunc sad_8x8_neon = vp8_sad8x8_neon;
const SadMxNFunc sad_4x4_neon = vp8_sad4x4_neon;
INSTANTIATE_TEST_CASE_P(NEON, SADTest, ::testing::Values(
                        make_tuple(16, 16, sad_16x16_neon, -1),
                        make_tuple(8, 16, sad_8x16_neon, -1),
                        make_tuple(16, 8, sad_16x8_neon, -1),
                        make_tuple(8, 8, sad_8x8_neon, -1),
                        make_tuple(4, 4, sad_4x4_neon, -1)));
#endif  // CONFIG_VP8_ENCODER
#if CONFIG_VP9_ENCODER
const SadMxNVp9Func sad_64x64_neon_vp9 = vp9_sad64x64_neon;
const SadMxNVp9Func sad_32x32_neon_vp9 = vp9_sad32x32_neon;
const SadMxNVp9Func sad_16x16_neon_vp9 = vp9_sad16x16_neon;
const SadMxNVp9Func sad_8x8_neon_vp9 = vp9_sad8x8_neon;
const SadMxNVp9Param neon_vp9_tests[] = {
  make_tuple(64, 64, sad_64x64_neon_vp9, -1),
  make_tuple(32, 32, sad_32x32_neon_vp9, -1),
  make_tuple(16, 16, sad_16x16_neon_vp9, -1),
  make_tuple(8, 8, sad_8x8_neon_vp9, -1),
};
INSTANTIATE_TEST_CASE_P(NEON, SADVP9Test, ::testing::ValuesIn(neon_vp9_tests));
#endif  // CONFIG_VP9_ENCODER
#endif  // HAVE_NEON

//------------------------------------------------------------------------------
// x86 functions
#if HAVE_MMX
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_mmx = vp8_sad16x16_mmx;
const SadMxNFunc sad_8x16_mmx = vp8_sad8x16_mmx;
const SadMxNFunc sad_16x8_mmx = vp8_sad16x8_mmx;
const SadMxNFunc sad_8x8_mmx = vp8_sad8x8_mmx;
const SadMxNFunc sad_4x4_mmx = vp8_sad4x4_mmx;
const SadMxNParam mmx_tests[] = {
  make_tuple(16, 16, sad_16x16_mmx, -1),
  make_tuple(8, 16, sad_8x16_mmx, -1),
  make_tuple(16, 8, sad_16x8_mmx, -1),
  make_tuple(8, 8, sad_8x8_mmx, -1),
  make_tuple(4, 4, sad_4x4_mmx, -1),
};
INSTANTIATE_TEST_CASE_P(MMX, SADTest, ::testing::ValuesIn(mmx_tests));
#endif  // CONFIG_VP8_ENCODER

#endif  // HAVE_MMX

#if HAVE_SSE
#if CONFIG_VP9_ENCODER
#if CONFIG_USE_X86INC
const SadMxNVp9Func sad_4x4_sse_vp9 = vp9_sad4x4_sse;
const SadMxNVp9Func sad_4x8_sse_vp9 = vp9_sad4x8_sse;
INSTANTIATE_TEST_CASE_P(SSE, SADVP9Test, ::testing::Values(
                        make_tuple(4, 4, sad_4x4_sse_vp9, -1),
                        make_tuple(4, 8, sad_4x8_sse_vp9, -1)));

const SadMxNx4Func sad_4x8x4d_sse = vp9_sad4x8x4d_sse;
const SadMxNx4Func sad_4x4x4d_sse = vp9_sad4x4x4d_sse;
INSTANTIATE_TEST_CASE_P(SSE, SADx4Test, ::testing::Values(
                        make_tuple(4, 8, sad_4x8x4d_sse, -1),
                        make_tuple(4, 4, sad_4x4x4d_sse, -1)));
#endif  // CONFIG_USE_X86INC
#endif  // CONFIG_VP9_ENCODER
#endif  // HAVE_SSE

#if HAVE_SSE2
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_wmt = vp8_sad16x16_wmt;
const SadMxNFunc sad_8x16_wmt = vp8_sad8x16_wmt;
const SadMxNFunc sad_16x8_wmt = vp8_sad16x8_wmt;
const SadMxNFunc sad_8x8_wmt = vp8_sad8x8_wmt;
const SadMxNFunc sad_4x4_wmt = vp8_sad4x4_wmt;
const SadMxNParam sse2_tests[] = {
  make_tuple(16, 16, sad_16x16_wmt, -1),
  make_tuple(8, 16, sad_8x16_wmt, -1),
  make_tuple(16, 8, sad_16x8_wmt, -1),
  make_tuple(8, 8, sad_8x8_wmt, -1),
  make_tuple(4, 4, sad_4x4_wmt, -1),
};
INSTANTIATE_TEST_CASE_P(SSE2, SADTest, ::testing::ValuesIn(sse2_tests));
#endif  // CONFIG_VP8_ENCODER

#if CONFIG_VP9_ENCODER
#if CONFIG_USE_X86INC
const SadMxNVp9Func sad_64x64_sse2_vp9 = vp9_sad64x64_sse2;
const SadMxNVp9Func sad_64x32_sse2_vp9 = vp9_sad64x32_sse2;
const SadMxNVp9Func sad_32x64_sse2_vp9 = vp9_sad32x64_sse2;
const SadMxNVp9Func sad_32x32_sse2_vp9 = vp9_sad32x32_sse2;
const SadMxNVp9Func sad_32x16_sse2_vp9 = vp9_sad32x16_sse2;
const SadMxNVp9Func sad_16x32_sse2_vp9 = vp9_sad16x32_sse2;
const SadMxNVp9Func sad_16x16_sse2_vp9 = vp9_sad16x16_sse2;
const SadMxNVp9Func sad_16x8_sse2_vp9 = vp9_sad16x8_sse2;
const SadMxNVp9Func sad_8x16_sse2_vp9 = vp9_sad8x16_sse2;
const SadMxNVp9Func sad_8x8_sse2_vp9 = vp9_sad8x8_sse2;
const SadMxNVp9Func sad_8x4_sse2_vp9 = vp9_sad8x4_sse2;

const SadMxNx4Func sad_64x64x4d_sse2 = vp9_sad64x64x4d_sse2;
const SadMxNx4Func sad_64x32x4d_sse2 = vp9_sad64x32x4d_sse2;
const SadMxNx4Func sad_32x64x4d_sse2 = vp9_sad32x64x4d_sse2;
const SadMxNx4Func sad_32x32x4d_sse2 = vp9_sad32x32x4d_sse2;
const SadMxNx4Func sad_32x16x4d_sse2 = vp9_sad32x16x4d_sse2;
const SadMxNx4Func sad_16x32x4d_sse2 = vp9_sad16x32x4d_sse2;
const SadMxNx4Func sad_16x16x4d_sse2 = vp9_sad16x16x4d_sse2;
const SadMxNx4Func sad_16x8x4d_sse2 = vp9_sad16x8x4d_sse2;
const SadMxNx4Func sad_8x16x4d_sse2 = vp9_sad8x16x4d_sse2;
const SadMxNx4Func sad_8x8x4d_sse2 = vp9_sad8x8x4d_sse2;
const SadMxNx4Func sad_8x4x4d_sse2 = vp9_sad8x4x4d_sse2;

#if CONFIG_VP9_HIGHBITDEPTH
const SadMxNVp9Func highbd_sad8x4_sse2_vp9 = vp9_highbd_sad8x4_sse2;
const SadMxNVp9Func highbd_sad8x8_sse2_vp9 = vp9_highbd_sad8x8_sse2;
const SadMxNVp9Func highbd_sad8x16_sse2_vp9 = vp9_highbd_sad8x16_sse2;
const SadMxNVp9Func highbd_sad16x8_sse2_vp9 = vp9_highbd_sad16x8_sse2;
const SadMxNVp9Func highbd_sad16x16_sse2_vp9 = vp9_highbd_sad16x16_sse2;
const SadMxNVp9Func highbd_sad16x32_sse2_vp9 = vp9_highbd_sad16x32_sse2;
const SadMxNVp9Func highbd_sad32x16_sse2_vp9 = vp9_highbd_sad32x16_sse2;
const SadMxNVp9Func highbd_sad32x32_sse2_vp9 = vp9_highbd_sad32x32_sse2;
const SadMxNVp9Func highbd_sad32x64_sse2_vp9 = vp9_highbd_sad32x64_sse2;
const SadMxNVp9Func highbd_sad64x32_sse2_vp9 = vp9_highbd_sad64x32_sse2;
const SadMxNVp9Func highbd_sad64x64_sse2_vp9 = vp9_highbd_sad64x64_sse2;

INSTANTIATE_TEST_CASE_P(SSE2, SADVP9Test, ::testing::Values(
                        make_tuple(64, 64, sad_64x64_sse2_vp9, -1),
                        make_tuple(64, 32, sad_64x32_sse2_vp9, -1),
                        make_tuple(32, 64, sad_32x64_sse2_vp9, -1),
                        make_tuple(32, 32, sad_32x32_sse2_vp9, -1),
                        make_tuple(32, 16, sad_32x16_sse2_vp9, -1),
                        make_tuple(16, 32, sad_16x32_sse2_vp9, -1),
                        make_tuple(16, 16, sad_16x16_sse2_vp9, -1),
                        make_tuple(16, 8, sad_16x8_sse2_vp9, -1),
                        make_tuple(8, 16, sad_8x16_sse2_vp9, -1),
                        make_tuple(8, 8, sad_8x8_sse2_vp9, -1),
                        make_tuple(8, 4, sad_8x4_sse2_vp9, -1),
                        make_tuple(8, 4, highbd_sad8x4_sse2_vp9, 8),
                        make_tuple(8, 8, highbd_sad8x8_sse2_vp9, 8),
                        make_tuple(8, 16, highbd_sad8x16_sse2_vp9, 8),
                        make_tuple(16, 8, highbd_sad16x8_sse2_vp9, 8),
                        make_tuple(16, 16, highbd_sad16x16_sse2_vp9, 8),
                        make_tuple(16, 32, highbd_sad16x32_sse2_vp9, 8),
                        make_tuple(32, 16, highbd_sad32x16_sse2_vp9, 8),
                        make_tuple(32, 32, highbd_sad32x32_sse2_vp9, 8),
                        make_tuple(32, 64, highbd_sad32x64_sse2_vp9, 8),
                        make_tuple(64, 32, highbd_sad64x32_sse2_vp9, 8),
                        make_tuple(64, 64, highbd_sad64x64_sse2_vp9, 8),
                        make_tuple(8, 4, highbd_sad8x4_sse2_vp9, 10),
                        make_tuple(8, 8, highbd_sad8x8_sse2_vp9, 10),
                        make_tuple(8, 16, highbd_sad8x16_sse2_vp9, 10),
                        make_tuple(16, 8, highbd_sad16x8_sse2_vp9, 10),
                        make_tuple(16, 16, highbd_sad16x16_sse2_vp9, 10),
                        make_tuple(16, 32, highbd_sad16x32_sse2_vp9, 10),
                        make_tuple(32, 16, highbd_sad32x16_sse2_vp9, 10),
                        make_tuple(32, 32, highbd_sad32x32_sse2_vp9, 10),
                        make_tuple(32, 64, highbd_sad32x64_sse2_vp9, 10),
                        make_tuple(64, 32, highbd_sad64x32_sse2_vp9, 10),
                        make_tuple(64, 64, highbd_sad64x64_sse2_vp9, 10),
                        make_tuple(8, 4, highbd_sad8x4_sse2_vp9, 12),
                        make_tuple(8, 8, highbd_sad8x8_sse2_vp9, 12),
                        make_tuple(8, 16, highbd_sad8x16_sse2_vp9, 12),
                        make_tuple(16, 8, highbd_sad16x8_sse2_vp9, 12),
                        make_tuple(16, 16, highbd_sad16x16_sse2_vp9, 12),
                        make_tuple(16, 32, highbd_sad16x32_sse2_vp9, 12),
                        make_tuple(32, 16, highbd_sad32x16_sse2_vp9, 12),
                        make_tuple(32, 32, highbd_sad32x32_sse2_vp9, 12),
                        make_tuple(32, 64, highbd_sad32x64_sse2_vp9, 12),
                        make_tuple(64, 32, highbd_sad64x32_sse2_vp9, 12),
                        make_tuple(64, 64, highbd_sad64x64_sse2_vp9, 12)));

const SadMxNAvgVp9Func highbd_sad8x4_avg_sse2_vp9 = vp9_highbd_sad8x4_avg_sse2;
const SadMxNAvgVp9Func highbd_sad8x8_avg_sse2_vp9 = vp9_highbd_sad8x8_avg_sse2;
const SadMxNAvgVp9Func highbd_sad8x16_avg_sse2_vp9 =
  vp9_highbd_sad8x16_avg_sse2;
const SadMxNAvgVp9Func highbd_sad16x8_avg_sse2_vp9 =
  vp9_highbd_sad16x8_avg_sse2;
const SadMxNAvgVp9Func highbd_sad16x16_avg_sse2_vp9 =
  vp9_highbd_sad16x16_avg_sse2;
const SadMxNAvgVp9Func highbd_sad16x32_avg_sse2_vp9 =
  vp9_highbd_sad16x32_avg_sse2;
const SadMxNAvgVp9Func highbd_sad32x16_avg_sse2_vp9 =
  vp9_highbd_sad32x16_avg_sse2;
const SadMxNAvgVp9Func highbd_sad32x32_avg_sse2_vp9 =
  vp9_highbd_sad32x32_avg_sse2;
const SadMxNAvgVp9Func highbd_sad32x64_avg_sse2_vp9 =
  vp9_highbd_sad32x64_avg_sse2;
const SadMxNAvgVp9Func highbd_sad64x32_avg_sse2_vp9 =
  vp9_highbd_sad64x32_avg_sse2;
const SadMxNAvgVp9Func highbd_sad64x64_avg_sse2_vp9 =
  vp9_highbd_sad64x64_avg_sse2;

INSTANTIATE_TEST_CASE_P(SSE2, SADavgVP9Test, ::testing::Values(
                        make_tuple(8, 4, highbd_sad8x4_avg_sse2_vp9, 8),
                        make_tuple(8, 8, highbd_sad8x8_avg_sse2_vp9, 8),
                        make_tuple(8, 16, highbd_sad8x16_avg_sse2_vp9, 8),
                        make_tuple(16, 8, highbd_sad16x8_avg_sse2_vp9, 8),
                        make_tuple(16, 16, highbd_sad16x16_avg_sse2_vp9, 8),
                        make_tuple(16, 32, highbd_sad16x32_avg_sse2_vp9, 8),
                        make_tuple(32, 16, highbd_sad32x16_avg_sse2_vp9, 8),
                        make_tuple(32, 32, highbd_sad32x32_avg_sse2_vp9, 8),
                        make_tuple(32, 64, highbd_sad32x64_avg_sse2_vp9, 8),
                        make_tuple(64, 32, highbd_sad64x32_avg_sse2_vp9, 8),
                        make_tuple(64, 64, highbd_sad64x64_avg_sse2_vp9, 8),
                        make_tuple(8, 4, highbd_sad8x4_avg_sse2_vp9, 10),
                        make_tuple(8, 8, highbd_sad8x8_avg_sse2_vp9, 10),
                        make_tuple(8, 16, highbd_sad8x16_avg_sse2_vp9, 10),
                        make_tuple(16, 8, highbd_sad16x8_avg_sse2_vp9, 10),
                        make_tuple(16, 16, highbd_sad16x16_avg_sse2_vp9, 10),
                        make_tuple(16, 32, highbd_sad16x32_avg_sse2_vp9, 10),
                        make_tuple(32, 16, highbd_sad32x16_avg_sse2_vp9, 10),
                        make_tuple(32, 32, highbd_sad32x32_avg_sse2_vp9, 10),
                        make_tuple(32, 64, highbd_sad32x64_avg_sse2_vp9, 10),
                        make_tuple(64, 32, highbd_sad64x32_avg_sse2_vp9, 10),
                        make_tuple(64, 64, highbd_sad64x64_avg_sse2_vp9, 10),
                        make_tuple(8, 4, highbd_sad8x4_avg_sse2_vp9, 12),
                        make_tuple(8, 8, highbd_sad8x8_avg_sse2_vp9, 12),
                        make_tuple(8, 16, highbd_sad8x16_avg_sse2_vp9, 12),
                        make_tuple(16, 8, highbd_sad16x8_avg_sse2_vp9, 12),
                        make_tuple(16, 16, highbd_sad16x16_avg_sse2_vp9, 12),
                        make_tuple(16, 32, highbd_sad16x32_avg_sse2_vp9, 12),
                        make_tuple(32, 16, highbd_sad32x16_avg_sse2_vp9, 12),
                        make_tuple(32, 32, highbd_sad32x32_avg_sse2_vp9, 12),
                        make_tuple(32, 64, highbd_sad32x64_avg_sse2_vp9, 12),
                        make_tuple(64, 32, highbd_sad64x32_avg_sse2_vp9, 12),
                        make_tuple(64, 64, highbd_sad64x64_avg_sse2_vp9, 12)));

const SadMxNx4Func highbd_sad_64x64x4d_sse2 = vp9_highbd_sad64x64x4d_sse2;
const SadMxNx4Func highbd_sad_64x32x4d_sse2 = vp9_highbd_sad64x32x4d_sse2;
const SadMxNx4Func highbd_sad_32x64x4d_sse2 = vp9_highbd_sad32x64x4d_sse2;
const SadMxNx4Func highbd_sad_32x32x4d_sse2 = vp9_highbd_sad32x32x4d_sse2;
const SadMxNx4Func highbd_sad_32x16x4d_sse2 = vp9_highbd_sad32x16x4d_sse2;
const SadMxNx4Func highbd_sad_16x32x4d_sse2 = vp9_highbd_sad16x32x4d_sse2;
const SadMxNx4Func highbd_sad_16x16x4d_sse2 = vp9_highbd_sad16x16x4d_sse2;
const SadMxNx4Func highbd_sad_16x8x4d_sse2 = vp9_highbd_sad16x8x4d_sse2;
const SadMxNx4Func highbd_sad_8x16x4d_sse2 = vp9_highbd_sad8x16x4d_sse2;
const SadMxNx4Func highbd_sad_8x8x4d_sse2 = vp9_highbd_sad8x8x4d_sse2;
const SadMxNx4Func highbd_sad_8x4x4d_sse2 = vp9_highbd_sad8x4x4d_sse2;
const SadMxNx4Func highbd_sad_4x8x4d_sse2 = vp9_highbd_sad4x8x4d_sse2;
const SadMxNx4Func highbd_sad_4x4x4d_sse2 = vp9_highbd_sad4x4x4d_sse2;

INSTANTIATE_TEST_CASE_P(SSE2, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, sad_64x64x4d_sse2, -1),
                        make_tuple(64, 32, sad_64x32x4d_sse2, -1),
                        make_tuple(32, 64, sad_32x64x4d_sse2, -1),
                        make_tuple(32, 32, sad_32x32x4d_sse2, -1),
                        make_tuple(32, 16, sad_32x16x4d_sse2, -1),
                        make_tuple(16, 32, sad_16x32x4d_sse2, -1),
                        make_tuple(16, 16, sad_16x16x4d_sse2, -1),
                        make_tuple(16, 8, sad_16x8x4d_sse2,  -1),
                        make_tuple(8, 16, sad_8x16x4d_sse2,  -1),
                        make_tuple(8, 8, sad_8x8x4d_sse2,   -1),
                        make_tuple(8, 4, sad_8x4x4d_sse2,   -1),
                        make_tuple(64, 64, highbd_sad_64x64x4d_sse2, 8),
                        make_tuple(64, 32, highbd_sad_64x32x4d_sse2, 8),
                        make_tuple(32, 64, highbd_sad_32x64x4d_sse2, 8),
                        make_tuple(32, 32, highbd_sad_32x32x4d_sse2, 8),
                        make_tuple(32, 16, highbd_sad_32x16x4d_sse2, 8),
                        make_tuple(16, 32, highbd_sad_16x32x4d_sse2, 8),
                        make_tuple(16, 16, highbd_sad_16x16x4d_sse2, 8),
                        make_tuple(16, 8, highbd_sad_16x8x4d_sse2,  8),
                        make_tuple(8, 16, highbd_sad_8x16x4d_sse2,  8),
                        make_tuple(8, 8, highbd_sad_8x8x4d_sse2,   8),
                        make_tuple(8, 4, highbd_sad_8x4x4d_sse2,   8),
                        make_tuple(4, 8, highbd_sad_4x8x4d_sse2,   8),
                        make_tuple(4, 4, highbd_sad_4x4x4d_sse2,   8),
                        make_tuple(64, 64, highbd_sad_64x64x4d_sse2, 10),
                        make_tuple(64, 32, highbd_sad_64x32x4d_sse2, 10),
                        make_tuple(32, 64, highbd_sad_32x64x4d_sse2, 10),
                        make_tuple(32, 32, highbd_sad_32x32x4d_sse2, 10),
                        make_tuple(32, 16, highbd_sad_32x16x4d_sse2, 10),
                        make_tuple(16, 32, highbd_sad_16x32x4d_sse2, 10),
                        make_tuple(16, 16, highbd_sad_16x16x4d_sse2, 10),
                        make_tuple(16, 8, highbd_sad_16x8x4d_sse2,  10),
                        make_tuple(8, 16, highbd_sad_8x16x4d_sse2,  10),
                        make_tuple(8, 8, highbd_sad_8x8x4d_sse2,   10),
                        make_tuple(8, 4, highbd_sad_8x4x4d_sse2,   10),
                        make_tuple(4, 8, highbd_sad_4x8x4d_sse2,   10),
                        make_tuple(4, 4, highbd_sad_4x4x4d_sse2,   10),
                        make_tuple(64, 64, highbd_sad_64x64x4d_sse2, 12),
                        make_tuple(64, 32, highbd_sad_64x32x4d_sse2, 12),
                        make_tuple(32, 64, highbd_sad_32x64x4d_sse2, 12),
                        make_tuple(32, 32, highbd_sad_32x32x4d_sse2, 12),
                        make_tuple(32, 16, highbd_sad_32x16x4d_sse2, 12),
                        make_tuple(16, 32, highbd_sad_16x32x4d_sse2, 12),
                        make_tuple(16, 16, highbd_sad_16x16x4d_sse2, 12),
                        make_tuple(16, 8, highbd_sad_16x8x4d_sse2,  12),
                        make_tuple(8, 16, highbd_sad_8x16x4d_sse2,  12),
                        make_tuple(8, 8, highbd_sad_8x8x4d_sse2,   12),
                        make_tuple(8, 4, highbd_sad_8x4x4d_sse2,   12),
                        make_tuple(4, 8, highbd_sad_4x8x4d_sse2,   12),
                        make_tuple(4, 4, highbd_sad_4x4x4d_sse2,   12)));
#else
INSTANTIATE_TEST_CASE_P(SSE2, SADVP9Test, ::testing::Values(
                        make_tuple(64, 64, sad_64x64_sse2_vp9, -1),
                        make_tuple(64, 32, sad_64x32_sse2_vp9, -1),
                        make_tuple(32, 64, sad_32x64_sse2_vp9, -1),
                        make_tuple(32, 32, sad_32x32_sse2_vp9, -1),
                        make_tuple(32, 16, sad_32x16_sse2_vp9, -1),
                        make_tuple(16, 32, sad_16x32_sse2_vp9, -1),
                        make_tuple(16, 16, sad_16x16_sse2_vp9, -1),
                        make_tuple(16, 8, sad_16x8_sse2_vp9, -1),
                        make_tuple(8, 16, sad_8x16_sse2_vp9, -1),
                        make_tuple(8, 8, sad_8x8_sse2_vp9, -1),
                        make_tuple(8, 4, sad_8x4_sse2_vp9, -1)));

INSTANTIATE_TEST_CASE_P(SSE2, SADx4Test, ::testing::Values(
                        make_tuple(64, 64, sad_64x64x4d_sse2, -1),
                        make_tuple(64, 32, sad_64x32x4d_sse2, -1),
                        make_tuple(32, 64, sad_32x64x4d_sse2, -1),
                        make_tuple(32, 32, sad_32x32x4d_sse2, -1),
                        make_tuple(32, 16, sad_32x16x4d_sse2, -1),
                        make_tuple(16, 32, sad_16x32x4d_sse2, -1),
                        make_tuple(16, 16, sad_16x16x4d_sse2, -1),
                        make_tuple(16, 8, sad_16x8x4d_sse2,  -1),
                        make_tuple(8, 16, sad_8x16x4d_sse2,  -1),
                        make_tuple(8, 8, sad_8x8x4d_sse2,   -1),
                        make_tuple(8, 4, sad_8x4x4d_sse2,   -1)));
#endif  // CONFIG_VP9_HIGHBITDEPTH
#endif  // CONFIG_USE_X86INC
#endif  // CONFIG_VP9_ENCODER
#endif  // HAVE_SSE2

#if HAVE_SSE3
#if CONFIG_VP8_ENCODER
const SadMxNx4Func sad_16x16x4d_sse3 = vp8_sad16x16x4d_sse3;
const SadMxNx4Func sad_16x8x4d_sse3 = vp8_sad16x8x4d_sse3;
const SadMxNx4Func sad_8x16x4d_sse3 = vp8_sad8x16x4d_sse3;
const SadMxNx4Func sad_8x8x4d_sse3 = vp8_sad8x8x4d_sse3;
const SadMxNx4Func sad_4x4x4d_sse3 = vp8_sad4x4x4d_sse3;
INSTANTIATE_TEST_CASE_P(SSE3, SADx4Test, ::testing::Values(
                        make_tuple(16, 16, sad_16x16x4d_sse3, -1),
                        make_tuple(16, 8, sad_16x8x4d_sse3, -1),
                        make_tuple(8, 16, sad_8x16x4d_sse3, -1),
                        make_tuple(8, 8, sad_8x8x4d_sse3, -1),
                        make_tuple(4, 4, sad_4x4x4d_sse3, -1)));
#endif  // CONFIG_VP8_ENCODER
#endif  // HAVE_SSE3

#if HAVE_SSSE3
#if CONFIG_USE_X86INC
#if CONFIG_VP8_ENCODER
const SadMxNFunc sad_16x16_sse3 = vp8_sad16x16_sse3;
INSTANTIATE_TEST_CASE_P(SSE3, SADTest, ::testing::Values(
                        make_tuple(16, 16, sad_16x16_sse3, -1)));
#endif  // CONFIG_VP8_ENCODER
#endif  // CONFIG_USE_X86INC
#endif  // HAVE_SSSE3

#if HAVE_AVX2
#if CONFIG_VP9_ENCODER
const SadMxNx4Func sad_64x64x4d_avx2 = vp9_sad64x64x4d_avx2;
const SadMxNx4Func sad_32x32x4d_avx2 = vp9_sad32x32x4d_avx2;
INSTANTIATE_TEST_CASE_P(AVX2, SADx4Test, ::testing::Values(
                        make_tuple(32, 32, sad_32x32x4d_avx2, -1),
                        make_tuple(64, 64, sad_64x64x4d_avx2, -1)));
#endif  // CONFIG_VP9_ENCODER
#endif  // HAVE_AVX2

}  // namespace
