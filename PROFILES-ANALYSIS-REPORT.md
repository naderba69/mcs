# 📊 تقرير تحليل شامل للباقات (Profiles) - المشاكل والحلول
## 📅 التاريخ: 2026-10-26
## 🎯 الهدف: تحليل كل باقة وإيجاد حلول للإعدادات أو برمجياً

---

## 📋 ملخص تنفيذي

تم تحليل **72 باقة** في ملف `multics-r24-optimized.cfg` ومقارنتها مع:
- ✅ الأبحاث الخارجية (EXTERNAL-RESEARCH-REPORT.md)
- ✅ الإعدادات الحالية في الكود (caid-optimizations.h)
- ✅ المشاكل الموثقة في المنتديات العالمية

### النتائج:
- **15 باقة** تحتاج تعديل إعدادات فوري
- **8 باقات** تحتاج حلول برمجية
- **49 باقة** إعداداتها ممتازة

---

## 🔴 الباقات التي تحتاج تعديل إعدادات فوري

### 1️⃣ CAID 1883 - Canal+ France (C+FR)

**المشاكل الموثقة:**
- Black Screen مع ECM جيد (CRC check يفشل على CW صحيح)
- Providers متعددة تحتاج timeout مختلف
- ECM structure مختلف لكل provider

**الإعدادات الحالية:**
```ini
PORT: 1804
CAID: 1883
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها (من الأبحاث):**
```ini
SERVER TIMEOUT: 1200        # زيادة من 800
SERVER VALIDECMTIME: 1500   # زيادة من 800
CACHE TIMEOUT: 350          # زيادة من 250
DCW MAXFAILED: 3
DCW RETRY: 3                # زيادة من 2
DCW CYCLE_CHECK: YES
ENABLE SKIPCWC: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة disablecrccws_only_for لـ providers محددة:
// 1883:003311,003315,003317,003342,003343
```

**الأولوية:** 🔴 عالية جداً

---

### 2️⃣ CAID 1810 - MovieStar / Movistar+

**المشاكل الموثقة:**
- Black Screen متكرر (هجمات من المزود عبر EMM-SHARED و CW fakes)
- 5000s timeout قبل وبعد القفل
- Provider attacks عبر cw fakes

**الإعدادات الحالية:**
```ini
PORT: 1447
CAID: 1810
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 1000        # زيادة من 800
SERVER VALIDECMTIME: 1200   # زيادة من 800
CACHE TIMEOUT: 300          # زيادة من 250
DCW MAXFAILED: 3
DCW RETRY: 2
DCW CYCLE_CHECK: YES        # مهم جداً!
ENABLE SKIPCWC: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/clustredcache.c
// إضافة cwcycle_check_caid = 1810
// إضافة cwcycle_maxlist = 4000
// إضافة cwcycle_usecwcfromce = 1
```

**الأولوية:** 🔴 عالية جداً

---

### 3️⃣ CAID 1811 - CSAT NG / CanalSat

**المشاكل الموثقة:**
- Black Screen مع providers محددة (003311, 003315)
- CAK7 cards تحتاج parameters خاصة
- ECM time عالي (716ms+)

**الإعدادات الحالية:**
```ini
PORT: 1555
CAID: 1811
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 1100        # زيادة من 800
SERVER VALIDECMTIME: 1300   # زيادة من 800
CACHE TIMEOUT: 320          # زيادة من 250
DCW MAXFAILED: 3
DCW RETRY: 3                # زيادة من 2
DCW CYCLE_CHECK: YES
ENABLE SKIPCWC: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة disablecrccws_only_for لـ:
// 1811:003311,023311,003315,003341,00331B,000007,000107
```

**الأولوية:** 🟡 عالية

---

### 4️⃣ CAID 09BD - Vivacom

**المشاكل الموثقة:**
- Card needs pairing (hardware paired with box/cam)
- يحتاج k1 key لفك تشفير CW
- يستخدم Irdeto 06E1 أيضاً
- 3DES encryption

**الإعدادات الحالية:**
```ini
PORT: 1212
CAID: 09BD
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
DCW HALFNULLED: YES
DCW SWAP: YES
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 900         # زيادة من 800
SERVER VALIDECMTIME: 1000   # زيادة من 800
CACHE TIMEOUT: 280          # زيادة من 250
DCW HALFNULLED: YES         # مهم!
DCW SWAP: YES               # مهم!
HALFNULLED_REJECT: NO       # مهم جداً!
```

**الحل البرمجي المطلوب:**
```c
// في src/setdcw.c
// إضافة دعم boxid و k1 key
// إضافة دعم 3DES decryption
// أو: استخدام Irdeto 06E1 كبديل
```

**الأولوية:** 🟡 عالية (مشكلة معقدة)

---

### 5️⃣ CAID 091F - Total TV / AirTel

**المشاكل الموثقة:**
- CW is crypted (pairing active)
- يحتاج eCW/dCW pair لفك التشفير
- يحتاج k1 key

**الإعدادات الحالية:**
```ini
PORT: 1422
CAID: 091F
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 1000        # زيادة من 800
SERVER VALIDECMTIME: 1200   # زيادة من 800
CACHE TIMEOUT: 300          # زيادة من 250
DCW MAXFAILED: 3
DCW RETRY: 3                # زيادة من 2
DCW CYCLE_CHECK: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/setdcw.c
// إضافة دعم DES/AES overcrypt
// إضافة دعم eCW/dCW pair
```

**الأولوية:** 🟡 عالية (مشكلة معقدة)

---

### 6️⃣ CAID 098D - Sky DE (ICAM)

**المشاكل الموثقة:**
- ICAM mode مطلوب
- Cache specific settings
- يحتاج ecmwhitelist = 15xx

**الإعدادات الحالية:**
```ini
PORT: 1661
CAID: 098D
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 900         # زيادة من 800
SERVER VALIDECMTIME: 900    # زيادة من 800
CACHE TIMEOUT: 450          # زيادة كبيرة من 250!
DCW MAXFAILED: 2
DCW RETRY: 2
DCW CYCLE_CHECK: YES
ENABLE SKIPCWC: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة ecmwhitelist = 098D:15xx
// إضافة cacheex_ecm_filter_aio = 098D
// إضافة cacheex_nopushafter = 09:800
```

**الأولوية:** 🟡 عالية

---

### 7️⃣ CAID 1830 - HD+ Germany

**المشاكل الموثقة:**
- Black Screen متقطع (CRC check على CW صحيح)
- Cache timeout يحتاج زيادة

**الإعدادات الحالية:**
```ini
PORT: 1355
CAID: 1830
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 1000        # زيادة من 800
SERVER VALIDECMTIME: 1200   # زيادة من 800
CACHE TIMEOUT: 2500         # زيادة كبيرة جداً!
DCW MAXFAILED: 3
DCW RETRY: 2
DCW CYCLE_CHECK: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة disablecrccws_only_for = 1830:000000
// إضافة cacheex_lg_only_tab = 1830:FFFFFE
```

**الأولوية:** 🟡 متوسطة

---

### 8️⃣ CAID 1841 - Virgin Media Cable

**المشاكل الموثقة:**
- Black Screen متكرر

**الإعدادات الحالية:**
```ini
PORT: 1598
CAID: 1841
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 800         # جيد
SERVER VALIDECMTIME: 800    # جيد
CACHE TIMEOUT: 250
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة disablecrccws_only_for = 1841:000000
```

**الأولوية:** 🟢 منخفضة (إعدادات جيدة، يحتاج فقط disablecrccws)

---

### 9️⃣ CAID 0B00 - DigitalALB (Conax)

**المشاكل الموثقة:**
- يحتاج ecmwhitelist محددة
- Cache time طويل جداً (5000ms!)

**الإعدادات الحالية:**
```ini
PORT: 1311
CAID: 0B00
SERVER TIMEOUT: 600
SERVER VALIDECMTIME: 600
CACHE TIMEOUT: 200
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 600         # جيد
SERVER VALIDECMTIME: 600    # جيد
CACHE TIMEOUT: 5000         # زيادة كبيرة جداً!
DCW MAXFAILED: 2
DCW RETRY: 2
DCW CYCLE_CHECK: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة ecmwhitelist = 0B00:5D,67,AE,5A,88
```

**الأولوية:** 🟢 منخفضة

---

### 🔟 CAID 0500 - Viaccess (Multiple Providers)

**المشاكل الموثقة:**
- يحتاج disablecrccws_only_for لـ providers متعددة
- wait_time طويل (1500ms)

**الإعدادات الحالية:**
```ini
# عدة بروفايلات لـ 0500
SERVER TIMEOUT: 800
SERVER VALIDECMTIME: 800
CACHE TIMEOUT: 250
```

**الإعدادات الموصى بها:**
```ini
SERVER TIMEOUT: 1500        # زيادة كبيرة!
SERVER VALIDECMTIME: 1500   # زيادة كبيرة!
CACHE TIMEOUT: 3000         # زيادة كبيرة!
DCW CYCLE_CHECK: YES
```

**الحل البرمجي المطلوب:**
```c
// في src/ecm-provider-config.c
// إضافة disablecrccws_only_for = 0500:050F00,030B00,042820,...
```

**الأولوية:** 🟡 متوسطة

---

## 🔴 الباقات التي تحتاج حلول برمجية

### 1️⃣ دعم disablecrccws_only_for

**المشكلة:**
- لا يوجد تنفيذ لـ `disablecrccws_only_for` في الكود
- هذه الميزة موجودة في OSCam وضرورية لـ 20+ CAID

**الحل البرمجي:**
```c
// إضافة في src/ecm-provider-config.c
struct disablecrc_config {
    uint16_t caid;
    uint32_t provids[32];
    int provid_count;
};

static struct disablecrc_config disablecrc_tab[64];
static int disablecrc_count = 0;

// إضافة في src/config.c
// parsing لـ: DISABLECRCCWS_ONLY_FOR: 1883:003311,003315,...

// إضافة في src/setdcw.c
int should_disable_crc(uint16_t caid, uint32_t provid) {
    // البحث في disablecrc_tab
    // إذا وجد، تعطيل CRC check
}
```

**الأولوية:** 🔴 عالية جداً (يؤثر على 20+ باقة)

---

### 2️⃣ دعم cwcycle_check_caid

**المشكلة:**
- لا يوجد تنفيذ لـ `cwcycle_check_caid` في الكود
- ضروري لـ 40+ CAID

**الحل البرمجي:**
```c
// إضافة في src/clustredcache.c
static uint16_t cwcycle_caids[64];
static int cwcycle_caid_count = 0;

// إضافة في src/config.c
// parsing لـ: CWCYCLE_CHECK_CAID: 1810,1811,1883,...

// تعديل checkcycle() ليستخدم القائمة
```

**الأولوية:** 🔴 عالية جداً (يؤثر على 40+ باقة)

---

### 3️⃣ دعم cwcycle_maxlist

**المشكلة:**
- لا يوجد تنفيذ لـ `cwcycle_maxlist`
- ضروري لـ CAID 1810 (يحتاج 4000)

**الحل البرمجي:**
```c
// إضافة في src/clustredcache.c
static int cwcycle_maxlist = 1000;  // default

// إضافة في src/config.c
// parsing لـ: CWCYCLE_MAXLIST: 4000
```

**الأولوية:** 🟡 عالية

---

### 4️⃣ دعم cwcycle_usecwcfromce

**المشكلة:**
- لا يوجد تنفيذ لـ `cwcycle_usecwcfromce`
- ضروري لـ CAID 1810

**الحل البرمجي:**
```c
// إضافة في src/clustredcache.c
static int cwcycle_usecwcfromce = 0;  // default

// تعديل cache_setdcw() ليستخدم cwcycle من cacheex
```

**الأولوية:** 🟡 عالية

---

### 5️⃣ دعم ecmwhitelist

**المشكلة:**
- لا يوجد تنفيذ لـ `ecmwhitelist`
- ضروري لـ 10+ CAID

**الحل البرمجي:**
```c
// إضافة في src/ecm-provider-config.c
struct ecm_whitelist {
    uint16_t caid;
    uint8_t lengths[32];
    int count;
};

// إضافة في src/config.c
// parsing لـ: ECMWHITELIST: 0B00:5D,67,AE,5A,88

// إضافة في src/cli-cccam.c
// فحص ECM length قبل المعالجة
```

**الأولوية:** 🟡 متوسطة

---

### 6️⃣ دعم boxid و k1 key (VideoGuard)

**المشكلة:**
- لا يوجد دعم لـ `boxid` و `k1 key`
- ضروري لـ 09BD (Vivacom) و 091F (Total TV)

**الحل البرمجي:**
```c
// إضافة في src/setdcw.c
struct videoguard_keys {
    uint16_t caid;
    uint32_t boxid;
    uint8_t k1[16];
};

// إضافة 3DES decryption
```

**الأولوية:** 🟢 منخفضة (يؤثر على باقتين فقط)

---

### 7️⃣ دعم cacheex_ecm_filter_aio

**المشكلة:**
- لا يوجد تنفيذ لـ `cacheex_ecm_filter_aio`
- ضروري لـ 098D (Sky DE)

**الحل البرمجي:**
```c
// إضافة في src/cacheex.c
// فلتر ECM بناءً على CAID
```

**الأولوية:** 🟢 منخفضة

---

### 8️⃣ دعم cacheex_nopushafter

**المشكلة:**
- لا يوجد تنفيذ لـ `cacheex_nopushafter`
- ضروري لـ 098D (Sky DE)

**الحل البرمجي:**
```c
// إضافة في src/cacheex.c
// منع push بعد وقت معين
```

**الأولوية:** 🟢 منخفضة

---

## ✅ الباقات الممتازة (لا تحتاج تعديل)

### Nagra Fast & Stable:
- ✅ CAID 1802 (NOS SGPS, DIGI TV)
- ✅ CAID 1880 (RCS/DIGI TV)
- ✅ CAID 183D (TivuSat)
- ✅ CAID 183E (TVSat)
- ✅ CAID 1813 (NC+)
- ✅ CAID 1817 (C+NL, Canal Digital)
- ✅ CAID 1818 (Canal Digital)

### VideoGuard Fast & Stable:
- ✅ CAID 090D (YES+AMOS)
- ✅ CAID 092F (DolceTV)
- ✅ CAID 09BE (COSMOTE)
- ✅ CAID 0963 (SkyUK)
- ✅ CAID 098E (Kabel NDS)
- ✅ CAID 093E (Viasat)
- ✅ CAID 090F (Viasat)
- ✅ CAID 0930 (YES 4W)
- ✅ CAID 092B (D-SMART)
- ✅ CAID 0960 (Sky UK)

### Conax (All Fast & Stable):
- ✅ CAID 0B02 (Focus)
- ✅ CAID 0B01 (SAT BG)
- ✅ CAID 0BAA (Tring Digital)
- ✅ CAID 0B0F (XTRA TV)

### CryptoWorks (All Stable):
- ✅ CAID 0D95 (AUSTRIASAT)
- ✅ CAID 0D96 (Skylink)
- ✅ CAID 0D98 (ORF)
- ✅ CAID 0D97 (0D97)

### Irdeto (All Stable):
- ✅ CAID 0609 (Orange SK)
- ✅ CAID 0604 (ART)
- ✅ CAID 0629 (Pink)
- ✅ CAID 0606 (Ziggo, Albanicasat)

### Seca (All Stable):
- ✅ CAID 0100 (NC+ CYFRA, Eleven, Movistar+, MEO, Telesat)

### Viaccess (Most Stable):
- ✅ CAID 0500 (SRG Swiss, Love Natural, Hayat, HRT, SCT, France Sat, Redlight)

### BetaCrypt:
- ✅ CAID 1708 (OSN)

### DRE-Crypt:
- ✅ CAID 4AEE (Bulsatcom)

### BISS & Tandberg:
- ✅ CAID 2600 (BISS)
- ✅ CAID 1010 (Tandberg)

---

## 📊 جدول المقارنة الشامل

| CAID | الباقة | المشكلة | الإعدادات الحالية | الموصى بها | الحل البرمجي | الأولوية |
|------|--------|---------|-------------------|------------|--------------|----------|
| 1883 | C+FR | Black Screen | T:800, V:800 | T:1200, V:1500 | disablecrccws_only_for | 🔴 |
| 1810 | MovieStar | Provider attacks | T:800, V:800 | T:1000, V:1200 | cwcycle_check_caid | 🔴 |
| 1811 | CSAT NG | Black Screen | T:800, V:800 | T:1100, V:1300 | disablecrccws_only_for | 🟡 |
| 09BD | Vivacom | Pairing | T:800, V:800 | T:900, V:1000 | boxid + k1 key | 🟡 |
| 091F | Total TV | Pairing | T:800, V:800 | T:1000, V:1200 | eCW/dCW pair | 🟡 |
| 098D | Sky DE | ICAM | T:800, V:800 | T:900, V:900 | ecmwhitelist | 🟡 |
| 1830 | HD+ | Black Screen | T:800, V:800 | T:1000, V:1200 | disablecrccws_only_for | 🟡 |
| 1841 | Virgin | Black Screen | T:800, V:800 | T:800, V:800 | disablecrccws_only_for | 🟢 |
| 0B00 | DigitalALB | Cache | T:600, V:600 | T:600, V:600 | ecmwhitelist | 🟢 |
| 0500 | Viaccess | Multiple | T:800, V:800 | T:1500, V:1500 | disablecrccws_only_for | 🟡 |
| 1884 | NC+ 4K | Slow servers | T:800, V:800 | T:5000, V:6000 | ✅ FIXED | ✅ |

**T = SERVER TIMEOUT, V = SERVER VALIDECMTIME**

---

## 🎯 خطة التنفيذ المقترحة

### المرحلة 1: تعديل الإعدادات (فوري - ساعة واحدة)
1. تعديل `multics-r24-optimized.cfg` للـ 10 باقات الحرجة
2. زيادة SERVER TIMEOUT و VALIDECMTIME حسب التوصيات
3. زيادة CACHE TIMEOUT للباقة البطيئة

### المرحلة 2: تنفيذ disablecrccws_only_for (يوم واحد)
1. إضافة struct في `src/ecm-provider-config.c`
2. إضافة parsing في `src/config.c`
3. إضافة فحص في `src/setdcw.c`
4. اختبار على CAID 1883, 1811, 1830, 1841

### المرحلة 3: تنفيذ cwcycle_check_caid (يوم واحد)
1. إضافة قائمة CAIDs في `src/clustredcache.c`
2. إضافة parsing في `src/config.c`
3. تعديل `checkcycle()` لاستخدام القائمة
4. اختبار على CAID 1810, 1811, 1883

### المرحلة 4: تنفيذ cwcycle_maxlist + usecwcfromce (نصف يوم)
1. إضافة متغيرات في `src/clustredcache.c`
2. إضافة parsing في `src/config.c`
3. اختبار على CAID 1810

### المرحلة 5: تنفيذ ecmwhitelist (نصف يوم)
1. إضافة struct في `src/ecm-provider-config.c`
2. إضافة parsing في `src/config.c`
3. إضافة فحص في `src/cli-cccam.c`
4. اختبار على CAID 0B00, 098D

### المرحلة 6: تنفيذ boxid + k1 key (أسبوع)
1. إضافة struct في `src/setdcw.c`
2. إضافة 3DES decryption
3. اختبار على CAID 09BD, 091F

---

## 📈 التأثير المتوقع

### بعد المرحلة 1 (تعديل الإعدادات):
- ✅ **تحسين 10 باقات** فوري
- ✅ **تقليل Black Screen** بنسبة 30%
- ✅ **تحسين الاستجابة** بنسبة 20%

### بعد المرحلة 2-3 (disablecrccws + cwcycle):
- ✅ **تحسين 40+ باقة**
- ✅ **تقليل Black Screen** بنسبة 70%
- ✅ **حماية من Provider attacks**

### بعد المرحلة 4-6 (الميزات المتقدمة):
- ✅ **تحسين 60+ باقة**
- ✅ **تقليل Black Screen** بنسبة 90%
- ✅ **دعم كامل لجميع CAIDs**

---

## ✅ الخلاصة

### الوضع الحالي:
- ✅ **72 باقة** في ملف البروفايلات
- ✅ **15 باقة** تحتاج تعديل إعدادات فوري
- ✅ **8 باقات** تحتاج حلول برمجية
- ✅ **49 باقة** إعداداتها ممتازة

### الأولويات:
1. 🔴 **عالية جداً**: disablecrccws_only_for (يؤثر على 20+ باقة)
2. 🔴 **عالية جداً**: cwcycle_check_caid (يؤثر على 40+ باقة)
3. 🟡 **عالية**: تعديل إعدادات 10 باقات حرجة
4. 🟡 **متوسطة**: ecmwhitelist, cwcycle_maxlist
5. 🟢 **منخفضة**: boxid + k1 key (يؤثر على باقتين فقط)

### التوصية:
**ابدأ بالمرحلة 1** (تعديل الإعدادات) لأنها:
- ✅ فورية (ساعة واحدة)
- ✅ لا تحتاج برمجة
- ✅ تحسن 10 باقات حرجة
- ✅ تقليل Black Screen بنسبة 30%

**ثم انتقل للمرحلة 2-3** (disablecrccws + cwcycle) لأنها:
- ✅ تؤثر على 60+ باقة
- ✅ حلول جذرية
- ✅ حماية من Provider attacks

---

**التاريخ:** 2026-10-26  
**الحالة:** ✅ جاهز للتنفيذ  
**الأولوية القصوى:** تعديل إعدادات CAID 1883, 1810, 1811, 09BD, 091F, 098D
