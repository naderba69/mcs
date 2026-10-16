#ifndef CWML_H
#define CWML_H

/*
 * CW Machine Learning — نهج تعلم الآلة
 * 
 * الفكرة: تدريب model على CWs صحيحة vs فايكة
 * Features: entropy, byte distribution, timing, source score, channel history
 * 
 * الميزة: دقة عالية
 * التحدي: يحتاج training data
 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#define CWML_FEATURE_COUNT 10
#define CWML_MAX_TRAINING 1000
#define CWML_THRESHOLD 0.7

/* ML features */
typedef struct {
    double entropy;
    double byte_uniformity;
    double zero_ratio;
    double ff_ratio;
    double sequential_score;
    double repeat_score;
    double source_score;
    double timing_score;
    double channel_history;
    double pattern_match;
} cwml_features_t;

/* Training sample */
typedef struct {
    cwml_features_t features;
    int label;  /* 1 = fake, 0 = good */
} cwml_sample_t;

/* ML model (simple logistic regression) */
typedef struct {
    double weights[CWML_FEATURE_COUNT];
    double bias;
    int trained;
} cwml_model_t;

/* ML state */
typedef struct {
    cwml_model_t model;
    cwml_sample_t training_data[CWML_MAX_TRAINING];
    int training_count;
    
    int total_predictions;
    int correct_predictions;
    int false_positives;
    int false_negatives;
} cwml_state_t;

static cwml_state_t g_cwml_state = {0};

/* تهيئة */
static inline void cwml_init(void) {
    memset(&g_cwml_state, 0, sizeof(g_cwml_state));
    
    /* Initialize weights to small random values */
    int i; for (i = 0; i < CWML_FEATURE_COUNT; i++) {
        g_cwml_state.model.weights[i] = 0.01 * (i % 3 - 1);
    }
    g_cwml_state.model.bias = 0.0;
}

/* Sigmoid function */
static inline double cwml_sigmoid(double x) {
    return 1.0 / (1.0 + exp(-x));
}

/* Extract features من CW */
static inline void cwml_extract_features(const unsigned char *cw, 
                                          double source_score,
                                          double timing_score,
                                          double channel_history,
                                          double pattern_match,
                                          cwml_features_t *features) {
    /* Feature 1: Entropy */
    int freq[256] = {0};
    int i; for (i = 0; i < 16; i++) {
        freq[cw[i]]++;
    }
    double entropy = 0.0;
    for (i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / 16.0;
            entropy -= p * log2(p);
        }
    }
    features->entropy = entropy / 4.0;  /* Normalize to 0-1 */
    
    /* Feature 2: Byte uniformity */
    double max_freq = 0;
    for (i = 0; i < 256; i++) {
        if (freq[i] > max_freq) max_freq = freq[i];
    }
    features->byte_uniformity = max_freq / 16.0;
    
    /* Feature 3: Zero ratio */
    features->zero_ratio = (double)freq[0x00] / 16.0;
    
    /* Feature 4: FF ratio */
    features->ff_ratio = (double)freq[0xFF] / 16.0;
    
    /* Feature 5: Sequential score */
    int sequential = 0;
    for (i = 1; i < 16; i++) {
        if (cw[i] == cw[i-1] + 1 || cw[i] == cw[i-1] - 1) {
            sequential++;
        }
    }
    features->sequential_score = (double)sequential / 15.0;
    
    /* Feature 6: Repeat score */
    int repeats = 0;
    for (i = 1; i < 16; i++) {
        if (cw[i] == cw[0]) repeats++;
    }
    features->repeat_score = (double)repeats / 15.0;
    
    /* Features 7-10: External scores */
    features->source_score = source_score;
    features->timing_score = timing_score;
    features->channel_history = channel_history;
    features->pattern_match = pattern_match;
}

/* Predict إذا كان CW فايك */
static inline double cwml_predict(const cwml_features_t *features) {
    if (!g_cwml_state.model.trained) {
        return 0.5;  /* No training yet */
    }
    
    double z = g_cwml_state.model.bias;
    const double *w = g_cwml_state.model.weights;
    
    z += w[0] * features->entropy;
    z += w[1] * features->byte_uniformity;
    z += w[2] * features->zero_ratio;
    z += w[3] * features->ff_ratio;
    z += w[4] * features->sequential_score;
    z += w[5] * features->repeat_score;
    z += w[6] * features->source_score;
    z += w[7] * features->timing_score;
    z += w[8] * features->channel_history;
    z += w[9] * features->pattern_match;
    
    return cwml_sigmoid(z);
}

/* إضافة training sample */
static inline void cwml_add_training_sample(const cwml_features_t *features, int label) {
    if (g_cwml_state.training_count >= CWML_MAX_TRAINING) {
        /* Remove oldest sample */
        memmove(&g_cwml_state.training_data[0], 
                &g_cwml_state.training_data[1],
                (CWML_MAX_TRAINING - 1) * sizeof(cwml_sample_t));
        g_cwml_state.training_count--;
    }
    
    cwml_sample_t *sample = &g_cwml_state.training_data[g_cwml_state.training_count++];
    sample->features = *features;
    sample->label = label;
}

/* تدريب Model (simple gradient descent) */
static inline void cwml_train(int iterations, double learning_rate) {
    if (g_cwml_state.training_count < 10) {
        return;  /* Not enough training data */
    }
    
    int iter;
    for (iter = 0; iter < iterations; iter++) {
        double total_error = 0.0;
        
        int i; for (i = 0; i < g_cwml_state.training_count; i++) {
            cwml_sample_t *sample = &g_cwml_state.training_data[i];
            
            /* Predict */
            double prediction = cwml_predict(&sample->features);
            double error = prediction - sample->label;
            total_error += error * error;
            
            /* Update weights */
            const double *f = (const double *)&sample->features;
            int j; for (j = 0; j < CWML_FEATURE_COUNT; j++) {
                g_cwml_state.model.weights[j] -= learning_rate * error * f[j];
            }
            g_cwml_state.model.bias -= learning_rate * error;
        }
        
        /* Check convergence */
        double avg_error = total_error / g_cwml_state.training_count;
        if (avg_error < 0.01) {
            break;
        }
    }
    
    g_cwml_state.model.trained = 1;
}

/* فحص CW */
static inline int cwml_is_fake(const unsigned char *cw,
                                double source_score,
                                double timing_score,
                                double channel_history,
                                double pattern_match) {
    g_cwml_state.total_predictions++;
    
    cwml_features_t features;
    cwml_extract_features(cw, source_score, timing_score, 
                          channel_history, pattern_match, &features);
    
    double prediction = cwml_predict(&features);
    
    return (prediction > CWML_THRESHOLD);
}

/* تسجيل نتيجة */
static inline void cwml_log_result(int predicted_fake, int actual_fake) {
    if (predicted_fake == actual_fake) {
        g_cwml_state.correct_predictions++;
    } else if (predicted_fake && !actual_fake) {
        g_cwml_state.false_positives++;
    } else {
        g_cwml_state.false_negatives++;
    }
}

/* الحصول على الإحصائيات */
static inline void cwml_get_stats(int *total, int *correct, int *fp, int *fn, int *training_samples) {
    if (total) *total = g_cwml_state.total_predictions;
    if (correct) *correct = g_cwml_state.correct_predictions;
    if (fp) *fp = g_cwml_state.false_positives;
    if (fn) *fn = g_cwml_state.false_negatives;
    if (training_samples) *training_samples = g_cwml_state.training_count;
}

/* تصفير الإحصائيات */
static inline void cwml_reset_stats(void) {
    g_cwml_state.total_predictions = 0;
    g_cwml_state.correct_predictions = 0;
    g_cwml_state.false_positives = 0;
    g_cwml_state.false_negatives = 0;
}

/* حساب الدقة */
static inline double cwml_get_accuracy(void) {
    if (g_cwml_state.total_predictions == 0) return 0.0;
    return (double)g_cwml_state.correct_predictions / g_cwml_state.total_predictions;
}

#endif /* CWML_H */
