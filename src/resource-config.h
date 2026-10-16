/* resource-config.h — Resource Usage Configuration
 *
 * PURPOSE: Control resource usage for low-end systems
 * 
 * Usage:
 *   - For high-end systems: Use default values
 *   - For low-end systems: Reduce table sizes
 *   - For minimal systems: Disable optimizations
 */

#ifndef RESOURCE_CONFIG_H
#define RESOURCE_CONFIG_H

// ============================================================================
// RESOURCE USAGE LEVELS
// ============================================================================

// Level 0: Minimal (disable all optimizations)
// Level 1: Low (small tables, basic features)
// Level 2: Medium (balanced)
// Level 3: High (full features, default)

#ifndef RESOURCE_LEVEL
#define RESOURCE_LEVEL 3  // Change this: 0, 1, 2, or 3
#endif

// ============================================================================
// TABLE SIZES (based on RESOURCE_LEVEL)
// ============================================================================

#if RESOURCE_LEVEL == 0
  // Minimal: Disable all optimizations
  #define HEALTH_MAX_SERVERS 0
  #define CACHE_PRIORITY_SLOTS 0
  #define PREFETCH_HISTORY_SIZE 0
  #define CW_HISTORY_SIZE 0
  #define PARALLEL_MAX_SERVERS 0
  #define WARMING_CHANNELS 0
  
#elif RESOURCE_LEVEL == 1
  // Low: Small tables for low-end systems
  #define HEALTH_MAX_SERVERS 32
  #define CACHE_PRIORITY_SLOTS 256
  #define PREFETCH_HISTORY_SIZE 10
  #define CW_HISTORY_SIZE 5
  #define PARALLEL_MAX_SERVERS 5
  #define WARMING_CHANNELS 20
  
#elif RESOURCE_LEVEL == 2
  // Medium: Balanced for average systems
  #define HEALTH_MAX_SERVERS 64
  #define CACHE_PRIORITY_SLOTS 512
  #define PREFETCH_HISTORY_SIZE 15
  #define CW_HISTORY_SIZE 8
  #define PARALLEL_MAX_SERVERS 8
  #define WARMING_CHANNELS 30
  
#else
  // High: Full features (default)
  #define HEALTH_MAX_SERVERS 128
  #define CACHE_PRIORITY_SLOTS 1024
  #define PREFETCH_HISTORY_SIZE 20
  #define CW_HISTORY_SIZE 10
  #define PARALLEL_MAX_SERVERS 10
  #define WARMING_CHANNELS 50
#endif

// ============================================================================
// FEATURE TOGGLES
// ============================================================================

// Enable/disable specific optimizations
#ifndef ENABLE_SERVER_HEALTH
#define ENABLE_SERVER_HEALTH 1
#endif

#ifndef ENABLE_CACHE_PRIORITY
#define ENABLE_CACHE_PRIORITY 1
#endif

#ifndef ENABLE_PREFETCH
#define ENABLE_PREFETCH 1
#endif

#ifndef ENABLE_CW_PREDICTION
#define ENABLE_CW_PREDICTION 1
#endif

#ifndef ENABLE_PARALLEL_ECM
#define ENABLE_PARALLEL_ECM 1
#endif

#ifndef ENABLE_AUTO_RECOVERY
#define ENABLE_AUTO_RECOVERY 1
#endif

#ifndef ENABLE_CW_ENCRYPTION
#define ENABLE_CW_ENCRYPTION 1
#endif

#ifndef ENABLE_PREDICTIVE_WARMING
#define ENABLE_PREDICTIVE_WARMING 1
#endif

// ============================================================================
// RESOURCE MONITORING
// ============================================================================

// Enable resource monitoring (adds minimal overhead)
#ifndef ENABLE_RESOURCE_MONITORING
#define ENABLE_RESOURCE_MONITORING 1
#endif

// Log resource usage every N seconds (0 = disabled)
#ifndef RESOURCE_LOG_INTERVAL
#define RESOURCE_LOG_INTERVAL 300  // 5 minutes
#endif

// ============================================================================
// MEMORY ESTIMATES
// ============================================================================

/*
 * Memory usage by RESOURCE_LEVEL:
 * 
 * Level 0 (Minimal):  ~0 KB (all optimizations disabled)
 * Level 1 (Low):      ~33 KB
 * Level 2 (Medium):   ~66 KB
 * Level 3 (High):     ~131 KB (default)
 * 
 * Choose based on your system:
 * - Embedded/Low-end: Use Level 1
 * - Average server: Use Level 2
 * - High-end server: Use Level 3
 */

#endif /* RESOURCE_CONFIG_H */
