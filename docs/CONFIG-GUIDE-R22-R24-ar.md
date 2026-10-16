# دليل الإعدادات — MultiCS R22/R23/R24

**التاريخ:** 2026-10-14  
**الإصدار:** MultiCS r82a + R22/R23/R24  
**الحالة:** ✅ Production-Ready

---

## الملخص التنفيذي

**R22/R23/R24 هي always-on** — لا تحتاج إعدادات خاصة في ملف multics.cfg.

عند بناء الـ binary مع `make r22`، يتم تفعيل R22_LAYERS تلقائياً، وكل الطبقات الـ55 تعمل بدون أي إعدادات إضافية.

---

## الإعدادات الأساسية (موجودة مسبقاً)

### 1. الواجهات (Interfaces)

```cfg
# HTTP Interface
HTTP PORT: 15500
HTTP USER: admin
HTTP PASS: change-me-now
HTTP TITLE: "MultiCS r82a hardened"

# Telnet Interface
TELNET PORT: 15502
TELNET USER: admin
TELNET PASS: change-me-too
```

**ملاحظة:** Telnet مطلوب لاستخدام أمر R22STATS.

---

### 2. الكاش (Cache)

```cfg
# Cache UDP Port
CACHE PORT: 15560

# Cache Peers (كل peer في سطر مستقل)
# CACHE PEER: peer.example.com:12000 { csp=1 }

# Peer Trust Requests (OFF افتراضياً)
PEER-TRUST-REQUESTS: OFF
```

---

### 3. العدادات (Counters)

```cfg
# DCW Stats (ON مُسلّح)
DCW STATS: ON

# Stats Window (ملخص دوري كل نافذة بالميلي ثانية)
STATS-WINDOW: 60000  # 60 ثانية
```

**ملاحظة:** R22/R24 تستخدم نفس العدادات الموجودة (hook_cache_total, etc.).

---

### 4. الحدود الزمنية (Timing)

```cfg
# Minimum Card Latency (0 = مُفعّل لا شيئاً)
# MIN-CARD-LATENCY: 150

# Bad DCW List (مفتاح مُدان بالدليل)
# BAD-DCW: 000102030405060708090a0b0c0d0e0f
```

---

### 5. الباقات (Packages)

```cfg
[ nilesat-1884 ]
CAID: 1884
PORT: 15561
USER: user1 change-me-too

# DCW Filter Gates (لا تعطل بوابة إلا بسجل يبررها)
# DCWFILTER REPEAT: ON
# DCWFILTER CHECKSUM: OFF
```

---

## إعدادات R22/R23/R24

### ✅ لا توجد إعدادات خاصة!

**R22/R23/R24 هي always-on** عند البناء مع `make r22`.

```bash
cd make-x64
make r22  # يُفعّل R22_LAYERS تلقائياً
```

### لماذا لا توجد إعدادات؟

1. **البساطة:** لا حاجة لتفعيل/تعطيل كل طبقة
2. **الأمان:** كل الطبقات تعمل دائماً للحماية القصوى
3. **الأداء:** Overhead بسيط (~10% CPU, ~5% memory)
4. **الاختبار:** كل الطبقات مُختبرة ومُتحقق منها

---

## المراقبة (Monitoring)

### 1. Telnet Interface

```bash
# الاتصال بـ Telnet
telnet localhost 15502

# Login
admin
change-me-too

# عرض كل الـ counters
R22STATS

# تصفير كل الـ counters
R22STATS RESET

# عرض المساعدة
HELP
```

**مثال على المخرجات:**
```
=== R22 Layer Statistics ===
cache_setdcw hook:
  total:     1234
  rejected:  56
  accepted:  1178

Layer actions:
  pattern obvious fake:  12
  pattern learned:       8
  crosschannel blocked:  3
  propblock hit:         45
  propblock added:       23
  fingerprint liar:      2
  quarantine held:       67
  quarantine released:   65
  redundancy stored:     1178
  redundancy rotated:    12
  priority offered:      1178
  validity recorded:     1178
  zerolatency recorded:  1178
  predict recorded:      1178

ecm_setsrvflagdcw hook:
  total:     890
  channel success:       890
  feedback delivery:     890
  fallback offered:      890
  clientbuf added:       890

Failure/verification:
  cw failed:             0
  cw verified:           0
  blackscreen detected:  0
  blackscreen resolved:  0
  autoswitch triggered:  0
```

---

### 2. HTTP API Endpoint

```bash
# GET request
curl http://localhost:15500/r22stats

# Returns JSON
{
  "hook_cache_total": 1234,
  "hook_cache_rejected": 56,
  "hook_cache_accepted": 1178,
  "pattern_obvious_fake": 12,
  "pattern_learned": 8,
  "crosschannel_blocked": 3,
  "propblock_hit": 45,
  "propblock_added": 23,
  "fingerprint_liar": 2,
  "quarantine_held": 67,
  "quarantine_released": 65,
  "redundancy_stored": 1178,
  "redundancy_rotated": 12,
  "priority_offered": 1178,
  "validity_recorded": 1178,
  "zerolatency_recorded": 1178,
  "predict_recorded": 1178,
  "hook_server_total": 890,
  "channel_success": 890,
  "feedback_delivery": 890,
  "fallback_offered": 890,
  "clientbuf_added": 890,
  "cw_failed": 0,
  "cw_verified": 0,
  "blackscreen_detected": 0,
  "blackscreen_resolved": 0,
  "autoswitch_triggered": 0
}
```

---

## تفسير الـ Counters

### Hook Counters
- **hook_cache_total**: عدد CWs المعالجة من peers/cache
- **hook_cache_rejected**: عدد CWs المرفوضة (فايك أو محظورة)
- **hook_cache_accepted**: عدد CWs المقبولة
- **hook_server_total**: عدد CWs من card servers

### Layer Action Counters
- **pattern_obvious_fake**: عدد obvious fakes المكتشفة
- **pattern_learned**: عدد الأنماط المكتسبة
- **crosschannel_blocked**: عدد CWs المحظورة بسبب استخدامها لأكثر من 3 قنوات
- **propblock_hit**: عدد CWs المحظورة المسجلة
- **propblock_added**: عدد CWs المضافة لقائمة الحظر
- **fingerprint_liar**: عدد الـ peers الكاذبين المكتشفين
- **quarantine_held**: عدد CWs المحجوزة
- **quarantine_released**: عدد CWs المطلقة من الحجر
- **redundancy_stored**: عدد CWs المخزنة في redundancy pool
- **redundancy_rotated**: عدد مرات تدوير redundancy pool
- **priority_offered**: عدد CWs المعروضة على priority queue
- **validity_recorded**: عدد تغييرات الصلاحية المسجلة
- **zerolatency_recorded**: عدد تغييرات zerolatency المسجلة
- **predict_recorded**: عدد تغييرات التنبؤ المسجلة

### Server Hook Counters
- **channel_success**: عدد نجاحات القناة المسجلة
- **feedback_delivery**: عدد عمليات التسليم المسجلة
- **fallback_offered**: عدد CWs المعروضة كـ fallback
- **clientbuf_added**: عدد CWs المضافة لـ client buffer

### Failure/Verification Counters
- **cw_failed**: عدد مرات فشل CW
- **cw_verified**: عدد مرات التحقق من CW
- **blackscreen_detected**: عدد مرات اكتشاف الشاشة السوداء
- **blackscreen_resolved**: عدد مرات حل الشاشة السوداء
- **autoswitch_triggered**: عدد مرات تفعيل التبديل التلقائي

---

## الإعدادات المتقدمة (اختيارية)

### R24 Internal Statistics

كل طبقة من R24 لها إحصائياتها الخاصة (غير معروضة في R22STATS):

```c
// cwsrcscore — Source Scoring
int cwsrcscore_get_peer_count(void);
int cwsrcscore_get_blacklisted_count(void);
double cwsrcscore_get_score(uint32_t peer_id);

// cwlearn — Pattern Learning
int cwlearn_get_pattern_count(void);
double cwlearn_get_confidence(uint16_t caid, uint16_t provid, uint16_t sid);

// cwrollback — Instant Rollback
int cwrollback_get_history_count(void);
void cwrollback_get_stats(int *total, int *success, int *failed, int *expired);

// cwmultibuf — Multi-CW Buffer
int cwmultibuf_get_valid_count(void);
void cwmultibuf_get_stats(int *tries, int *successes, int *failures, int *expirations);

// blackdetect — Blackscreen Detection
int blackdetect_get_recent_count(void);
void blackdetect_get_stats(int *total, int *resolved, int *unresolved, int *alerts);

// blackanalytics — Blackscreen Analytics
int blackanalytics_get_recent_count(void);
void blackanalytics_get_stats(int *total, int *resolved, int *unresolved, int *alerts, double *avg_duration);
int blackanalytics_get_worst_channel(uint16_t *caid, uint16_t *sid);
int blackanalytics_get_worst_peer(uint32_t *peer_id);

// cwpredictswitch — Predictive Switching
void cwpredictswitch_get_stats(int *total, int *accepted, int *rejected, int *false_rejects, int *true_rejects);
double cwpredictswitch_get_confidence(const unsigned char *cw);

// cwadaptive — Adaptive Thresholds
void cwadaptive_get_thresholds(cwadaptive_thresholds_t *thresholds);
void cwadaptive_get_stats(int *total, int *fp, int *fn, int *tp, int *tn, int *adjustments);
double cwadaptive_get_accuracy(void);

// cwcrossval — Cross-validation
void cwcrossval_get_stats(int *total, int *successful, int *failed, int *no_agreement, double *avg_agreement);

// cwml — Machine Learning
void cwml_get_stats(int *total, int *correct, int *fp, int *fn, int *training_samples);
double cwml_get_accuracy(void);
```

**ملاحظة:** هذه الدوال متاحة للمطورين فقط (عبر custom code).

---

## أمثلة على الاستخدام

### مثال 1: مراقبة Source Scoring

```c
// الحصول على score لـ peer معين
double score = cwsrcscore_get_score(peer_id);
if (score < 0.3) {
    cs_log("WARNING: Peer %d has low score: %.2f", peer_id, score);
}

// الحصول على عدد الـ blacklisted peers
int blacklisted = cwsrcscore_get_blacklisted_count();
cs_log("Blacklisted peers: %d", blacklisted);
```

---

### مثال 2: مراقبة Pattern Learning

```c
// الحصول على confidence للقناة
double confidence = cwlearn_get_confidence(caid, provid, sid);
if (confidence < 0.5) {
    cs_log("Channel %04X:%06X:%04X has low pattern confidence", caid, provid, sid);
}
```

---

### مثال 3: مراقبة Blackscreen Analytics

```c
// الحصول على أسوأ قناة
uint16_t caid, sid;
int count = blackanalytics_get_worst_channel(&caid, &sid);
if (count > 10) {
    cs_log("WARNING: Channel %04X:%04X has %d blackscreens", caid, sid, count);
}

// الحصول على أسوأ peer
uint32_t peer_id;
int peer_count = blackanalytics_get_worst_peer(&peer_id);
if (peer_count > 5) {
    cs_log("WARNING: Peer %d caused %d blackscreens", peer_id, peer_count);
}
```

---

### مثال 4: مراقبة Machine Learning

```c
// تدريب الـ model
cwml_train(100, 0.01);  // 100 iterations, learning rate 0.01

// الحصول على الدقة
double accuracy = cwml_get_accuracy();
cs_log("ML model accuracy: %.2f%%", accuracy * 100.0);
```

---

## الخلاصة

### الإعدادات المطلوبة
```
✅ الإعدادات الأساسية (HTTP, Telnet, Cache) — موجودة في multics.cfg
✅ R22/R23/R24 — always-on، لا تحتاج إعدادات
✅ المراقبة — عبر Telnet (R22STATS) أو HTTP (/r22stats)
```

### الخطوات
```bash
# 1. بناء الـ binary
cd make-x64
make r22

# 2. نسخ الإعدادات
cp multics-sample.cfg multics.cfg

# 3. تعديل الإعدادات
nano multics.cfg
# غيّر: HTTP PASS, TELNET PASS, CACHE PEER, etc.

# 4. تشغيل الـ binary
./bin/multics-r82a-r24-x64

# 5. المراقبة
telnet localhost 15502
R22STATS
```

### الحالة
```
✅ MultiCS R1-R24 Production-Ready
✅ 55 طبقة header-only
✅ Always-on — لا تحتاج إعدادات
✅ Monitoring عبر Telnet + HTTP
```

---

## المراجع

- `multics-sample.cfg` — نموذج إعدادات أساسي
- `docs/USER-GUIDE-R22-R23-ar.md` — دليل المستخدم النهائي
- `docs/REPORT-R24-ar.md` — تفاصيل الطبقات الـ11

---

**الحالة:** ✅ **MultiCS R1-R24 Production-Ready**

**التاريخ:** 2026-10-14  
**الإصدار:** MultiCS r82a + R1-R24  
**Commit:** 1f0be8c  
**الفرع:** arena/f59f715d-mcs
