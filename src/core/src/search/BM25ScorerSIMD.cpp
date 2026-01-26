// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BM25ScorerSIMD.h"

namespace diagon {
namespace search {

BM25ScorerSIMD::BM25ScorerSIMD(const Weight& weight, std::unique_ptr<index::PostingsEnum> postings,
                               float idf, float k1, float b)
    : weight_(weight)
    , postings_(std::move(postings))
    , doc_(-1)
    , currentScore_(0.0f)
    , idf_(idf)
    , k1_(k1)
    , b_(b)
    , k1_plus_1_(k1 + 1.0f) {
#ifdef DIAGON_HAVE_AVX2
    // Precompute SIMD constants (AVX2: 8 floats)
    idf_vec_ = _mm256_set1_ps(idf_);
    k1_vec_ = _mm256_set1_ps(k1_);
    b_vec_ = _mm256_set1_ps(b_);
    k1_plus_1_vec_ = _mm256_set1_ps(k1_plus_1_);
    one_minus_b_vec_ = _mm256_set1_ps(1.0f - b_);
    avgFieldLength_vec_ = _mm256_set1_ps(1.0f);
#elif defined(DIAGON_HAVE_NEON)
    // Precompute SIMD constants (NEON: 4 floats)
    idf_vec_ = vdupq_n_f32(idf_);
    k1_vec_ = vdupq_n_f32(k1_);
    b_vec_ = vdupq_n_f32(b_);
    k1_plus_1_vec_ = vdupq_n_f32(k1_plus_1_);
    one_minus_b_vec_ = vdupq_n_f32(1.0f - b_);
    avgFieldLength_vec_ = vdupq_n_f32(1.0f);
#endif
}

int BM25ScorerSIMD::nextDoc() {
    doc_ = postings_->nextDoc();
    if (doc_ != index::PostingsEnum::NO_MORE_DOCS) {
        int freq = postings_->freq();
        long norm = 1L;  // Phase 4: Simplified norm
        currentScore_ = scoreScalar(freq, norm);
    }
    return doc_;
}

int BM25ScorerSIMD::docID() const {
    return doc_;
}

float BM25ScorerSIMD::score() const {
    return currentScore_;
}

int BM25ScorerSIMD::advance(int target) {
    doc_ = postings_->advance(target);
    if (doc_ != index::PostingsEnum::NO_MORE_DOCS) {
        int freq = postings_->freq();
        long norm = 1L;
        currentScore_ = scoreScalar(freq, norm);
    }
    return doc_;
}

int64_t BM25ScorerSIMD::cost() const {
    return postings_ ? postings_->cost() : 0;
}

const Weight& BM25ScorerSIMD::getWeight() const {
    return weight_;
}

float BM25ScorerSIMD::scoreScalar(int freq, long norm) const {
    if (freq == 0)
        return 0.0f;

    // Decode norm to field length
    // Phase 4: Simplified - returns 1.0
    float fieldLength = 1.0f;
    float avgFieldLength = 1.0f;

    // BM25 formula:
    // score = idf * freq * (k1 + 1) / (freq + k)
    // where k = k1 * (1 - b + b * fieldLength / avgFieldLength)
    float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
    float freqFloat = static_cast<float>(freq);

    return idf_ * freqFloat * k1_plus_1_ / (freqFloat + k);
}

#ifdef DIAGON_HAVE_AVX2

void BM25ScorerSIMD::scoreBatch(const int* freqs, const long* norms, float* scores) const {
    // Load 8 frequencies as integers
    __m256i freq_ints = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(freqs));

    // Convert frequencies to floats
    __m256 freq_floats = int32ToFloat(freq_ints);

    // Load and decode 8 norms
    // Phase 4: Simplified - all norms decode to 1.0
    // Phase 5 will implement proper norm decoding
    __m256 fieldLengths = _mm256_set1_ps(1.0f);

    // Compute k = k1 * (1 - b + b * fieldLength / avgFieldLength)
    // k = k1 * (1 - b + b * fieldLength)  [since avgFieldLength = 1.0 in Phase 4]
    __m256 b_times_fieldLength = _mm256_mul_ps(b_vec_, fieldLengths);
    __m256 one_minus_b_plus_term = _mm256_add_ps(one_minus_b_vec_, b_times_fieldLength);
    __m256 k = _mm256_mul_ps(k1_vec_, one_minus_b_plus_term);

    // Compute numerator = idf * freq * (k1 + 1)
    __m256 numerator = _mm256_mul_ps(idf_vec_, freq_floats);
    numerator = _mm256_mul_ps(numerator, k1_plus_1_vec_);

    // Compute denominator = freq + k
    __m256 denominator = _mm256_add_ps(freq_floats, k);

    // Compute score = numerator / denominator
    __m256 score_vec = _mm256_div_ps(numerator, denominator);

    // Store results
    _mm256_storeu_ps(scores, score_vec);
}

void BM25ScorerSIMD::scoreBatchUniformNorm(const int* freqs, long norm, float* scores) const {
    // Load 8 frequencies as integers
    __m256i freq_ints = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(freqs));

    // Convert frequencies to floats
    __m256 freq_floats = int32ToFloat(freq_ints);

    // Uniform field length (same for all 8 documents)
    // Phase 4: Simplified - norm decodes to 1.0
    __m256 fieldLength = _mm256_set1_ps(1.0f);

    // Compute k = k1 * (1 - b + b * fieldLength / avgFieldLength)
#    ifdef DIAGON_HAVE_FMA
    // Use FMA for better performance and accuracy
    // k = k1 * (one_minus_b + b * fieldLength)
    __m256 k = _mm256_fmadd_ps(b_vec_, fieldLength, one_minus_b_vec_);
    k = _mm256_mul_ps(k1_vec_, k);
#    else
    __m256 b_times_fieldLength = _mm256_mul_ps(b_vec_, fieldLength);
    __m256 one_minus_b_plus_term = _mm256_add_ps(one_minus_b_vec_, b_times_fieldLength);
    __m256 k = _mm256_mul_ps(k1_vec_, one_minus_b_plus_term);
#    endif

    // Compute numerator = idf * freq * (k1 + 1)
    __m256 numerator;
#    ifdef DIAGON_HAVE_FMA
    // numerator = idf * freq * k1_plus_1
    numerator = _mm256_mul_ps(idf_vec_, freq_floats);
    numerator = _mm256_mul_ps(numerator, k1_plus_1_vec_);
#    else
    numerator = _mm256_mul_ps(idf_vec_, freq_floats);
    numerator = _mm256_mul_ps(numerator, k1_plus_1_vec_);
#    endif

    // Compute denominator = freq + k
    __m256 denominator = _mm256_add_ps(freq_floats, k);

    // Compute score = numerator / denominator
    __m256 score_vec = _mm256_div_ps(numerator, denominator);

    // Store results
    _mm256_storeu_ps(scores, score_vec);
}

__m256 BM25ScorerSIMD::decodeNormsVec(const __m256i norms_vec) const {
    // Phase 4: Simplified - all norms decode to 1.0
    // Phase 5 will implement proper Lucene norm decoding
    return _mm256_set1_ps(1.0f);
}

__m256 BM25ScorerSIMD::int32ToFloat(__m256i int_vec) const {
    // Convert 8 x int32 to 8 x float
    return _mm256_cvtepi32_ps(int_vec);
}

#endif  // DIAGON_HAVE_AVX2

// ==================== ARM NEON Implementation ====================

#ifdef DIAGON_HAVE_NEON

void BM25ScorerSIMD::scoreBatch(const int* freqs, const long* norms, float* scores) const {
    // Load 4 frequencies as integers
    IntVec freq_ints = vld1q_s32(freqs);

    // Convert frequencies to floats
    FloatVec freq_floats = int32ToFloat(freq_ints);

    // Load and decode 4 norms
    // Phase 4: Simplified - all norms decode to 1.0
    FloatVec fieldLengths = vdupq_n_f32(1.0f);

    // Compute k = k1 * (1 - b + b * fieldLength / avgFieldLength)
    // k = k1 * (1 - b + b * fieldLength)  [since avgFieldLength = 1.0 in Phase 4]
    FloatVec b_times_fieldLength = vmulq_f32(b_vec_, fieldLengths);
    FloatVec one_minus_b_plus_term = vaddq_f32(one_minus_b_vec_, b_times_fieldLength);
    FloatVec k = vmulq_f32(k1_vec_, one_minus_b_plus_term);

    // Compute numerator = idf * freq * (k1 + 1)
    FloatVec numerator = vmulq_f32(idf_vec_, freq_floats);
    numerator = vmulq_f32(numerator, k1_plus_1_vec_);

    // Compute denominator = freq + k
    FloatVec denominator = vaddq_f32(freq_floats, k);

    // Compute score = numerator / denominator
    // NEON doesn't have direct division, use reciprocal estimate + Newton-Raphson
    FloatVec recip = vrecpeq_f32(denominator);  // Initial estimate
    recip = vmulq_f32(vrecpsq_f32(denominator, recip), recip);  // Newton-Raphson iteration
    FloatVec score_vec = vmulq_f32(numerator, recip);

    // Store results
    vst1q_f32(scores, score_vec);
}

void BM25ScorerSIMD::scoreBatchUniformNorm(const int* freqs, long norm, float* scores) const {
    // Load 4 frequencies as integers
    IntVec freq_ints = vld1q_s32(freqs);

    // Convert frequencies to floats
    FloatVec freq_floats = int32ToFloat(freq_ints);

    // Uniform field length (same for all 4 documents)
    // Phase 4: Simplified - norm decodes to 1.0
    FloatVec fieldLength = vdupq_n_f32(1.0f);

    // Compute k = k1 * (1 - b + b * fieldLength / avgFieldLength)
    // Use FMA if available (ARMv8.2+)
#ifdef __ARM_FEATURE_FMA
    // k = k1 * (one_minus_b + b * fieldLength)
    FloatVec k = vfmaq_f32(one_minus_b_vec_, b_vec_, fieldLength);
    k = vmulq_f32(k1_vec_, k);
#else
    FloatVec b_times_fieldLength = vmulq_f32(b_vec_, fieldLength);
    FloatVec one_minus_b_plus_term = vaddq_f32(one_minus_b_vec_, b_times_fieldLength);
    FloatVec k = vmulq_f32(k1_vec_, one_minus_b_plus_term);
#endif

    // Compute numerator = idf * freq * (k1 + 1)
    FloatVec numerator = vmulq_f32(idf_vec_, freq_floats);
    numerator = vmulq_f32(numerator, k1_plus_1_vec_);

    // Compute denominator = freq + k
    FloatVec denominator = vaddq_f32(freq_floats, k);

    // Compute score = numerator / denominator
    // Use reciprocal with Newton-Raphson refinement for better accuracy
    FloatVec recip = vrecpeq_f32(denominator);
    recip = vmulq_f32(vrecpsq_f32(denominator, recip), recip);
    FloatVec score_vec = vmulq_f32(numerator, recip);

    // Store results
    vst1q_f32(scores, score_vec);
}

BM25ScorerSIMD::FloatVec BM25ScorerSIMD::decodeNormsVec(const IntVec norms_vec) const {
    // Phase 4: Simplified - all norms decode to 1.0
    return vdupq_n_f32(1.0f);
}

BM25ScorerSIMD::FloatVec BM25ScorerSIMD::int32ToFloat(IntVec int_vec) const {
    // Convert 4 x int32 to 4 x float
    return vcvtq_f32_s32(int_vec);
}

#endif  // DIAGON_HAVE_NEON

// ==================== Factory Function ====================

std::unique_ptr<BM25ScorerSIMD> createBM25Scorer(const Weight& weight,
                                                 std::unique_ptr<index::PostingsEnum> postings,
                                                 float idf, float k1, float b) {
    return std::make_unique<BM25ScorerSIMD>(weight, std::move(postings), idf, k1, b);
}

}  // namespace search
}  // namespace diagon
