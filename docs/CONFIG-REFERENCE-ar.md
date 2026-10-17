> ⚠️ **وثيقة تاريخية (عهد R22–R24، حتى 2026-10-15).** تشرح حالة ذلك العهد وبعض أنظمته كودٌ ميت لا يُبنى اليوم، فلا تتّبعها كدليل حالي. المرجع الحيّ: `../CHANGELOG.md` و`docs/FIELD-GUIDE-ar.md` وتقارير R26–R32. أُبقيت كما هي سجلًّا تاريخيًّا.

# مرجع الإعداد — MultiCS r82a (2026-10-14)

مرجع شامل لكل مفاتيح ملف الإعداد `multics.cfg`. كل مفتاح موثَّق بالوظيفة، النوع، الافتراضي، والمثال.

**قاعدة:** كل خيار جديد من R13–R20 موثَّق هنا بعلامته ومصدره. الافتراضي دائماً = سلوك r82a الأصلي (OFF أو القيمة القديمة) ما لم يُذكر خلافه.

---

## 1. المفاتيح العالمية

### 1.1 واجهات الإدارة

| المفتاح | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `HTTP PORT` | رقم | 5500 | منفذ واجهة الويب |
| `HTTP USER` | نص | — | اسم مستخدم الويب |
| `HTTP PASS` | نص | — | كلمة مرور الويب |
| `HTTP TITLE` | نص | `multics` | عنوان الصفحة |
| `TELNET PORT` | رقم | — | منفذ تيلنت (غيابه = لا تيلنت) |
| `TELNET USER` | نص | — | اسم مستخدم تيلنت |
| `TELNET PASS` | نص | — | كلمة مرور تيلنت |

### 1.2 خوادم البطاقات

| المفتاح | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `CAMD35 PORT` | رقم | — | منفذ camd35 للخوادم (غيابه = لا camd35) |
| `CCCAM PORT` | رقم | — | منفذ cccam للخوادم |
| `CS378X PORT` | رقم | — | منفذ cs378x للخوادم |
| `NEWCAMD PORT` | رقم | — | منفذ newcamd للعملاء |
| `MGCAMD PORT` | رقم | — | منفذ mgcamd |
| `FREECCCAM PORT` | رقم | — | منفذ freecccam |
| `RADEGAST PORT` | رقم | — | منفذ radegast |

### 1.3 شبكة الكاش

| المفتاح | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `CACHE PORT` | رقم | — | منفذ استقبال الكاش (UDP) — **مطلوب** إذا وُجد `CACHE PEER` |
| `CACHE PEER` | `host:port { opts }` | — | نظير كاش (يمكن تكراره) |
| `CACHE FILTER` | ON/OFF | ON | فلترة الكاش |
| `CACHE THRESHOLD` | رقم | 2 | عتبة اتفاق الكاش |
| `CACHE AUTOADD` | `yes/no, yes/no` | — | إضافة تلقائية للنظراء الجدد |
| `CACHEEX MAXHOP` | رقم (1..2) | 2 | أفق القفزات (stock r82a) |
| `CACHEEX MAXHOP_LG` | رقم (1..10) | — | **R18:** أفق القفزات الموسّع — آخر سطر يفوز |

**تحذير `CACHE PORT`:** وجود `CACHE PEER` دون `CACHE PORT` يسبب segfault في r82a — لا تبدأ الخادم بدونه.

### 1.4 الإحصاءات والرصد

| المفتاح | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `DCW STATS` | ON/OFF | OFF | تفعيل صفحة `/cwlog` و`/dcwstats` |
| `STATS WINDOW` | رقم (ثواني) | 0 | نافذة الإحصاءات المتحركة |
| `CACHE LATE-TRUTH` | ON/OFF | OFF | **R13:** تصحيح الحقيقة المتأخرة |
| `PEER-TRUST-STORE` | ON/OFF | OFF | **R14:** حارس التلوث (التخزين/إعادة الضخ) |

### 1.5 الفلترة والأمان

| المفتاح | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `DCWFILTER` | ON/OFF | ON | فلترة DCW |
| `BAD-DCW` | قائمة | — | قائمة CW سيئة معروفة |
| `BAD-CW-LIMIT` | رقم | 1 | حدّ النزاعات قبل التجنب |
| `SERVICE-BLACKLIST-TIME` | رقم (ثواني) | 0 | مدة كبت القناة المرفوضة |
| `MIN-CARD-LATENCY` | رقم (ms) | 0 | أرضية زمن ردّ البطاقة |
| `VALIDECMTIME` | رقم (ثواني) | — | صلاحية ECM |

---

## 2. مفاتيح CAID FAMILY (R16)

### 2.1 `CAID FAMILY HALFNULLED`

```cfg
CAID FAMILY HALFNULLED: 09, 0E
```

**الوظيفة:** عائلات CA التي يُقبل فيها المزوّد بنصفه المُلغى (مثل NDS).  
**السلوك:** يملك المجموعة ويُسقط الافتراضي. قائمة فارغة = لا أسرة. غياب الأسطر = r82a الأصلي.

### 2.2 `CAID FAMILY NOPROVCHK`

```cfg
CAID FAMILY NOPROVCHK: 18, 09, 0B
```

**الوظيفة:** عائلات CA التي يُتجاوز فيها فحص المزوّد.  
**السلوك:** تراكُم مع `HALFNULLED`. أول سطر للعلامة يملك المجموعة.

**الانحراف الوحيد عن r82a:** الأسر الغريبة (خارج القائمتين) تُطابَق الآن «بالمزوّد بالضبط» على مسار loadbalance. موثَّق في `COMPAT-ar.md`.

### 2.3 `CAID FAMILY PROVIDERCHECK`

```
CAID FAMILY PROVIDERCHECK: 01, 05
```

**السلوك:** عائلات تُطابَق بطاقاتها بصرامة المزوّد في اختيار البطاقة
(loadbalance): بطاقة من عائلة خارج المجموعة تُعادل أي بطاقة بنفس
CAID عند غياب تطابق المزوّد. الافتراضي `{01, 05}` هو قاعدة stock
العكسية حرفيًا (SECA/Viaccess صارمتان فقط). نفس قاعدة التملّك:
أول سطر يملك المجموعة. (R39/D98 — الجدول واحد للقواعد الثلاث.)


---

## 3. خيارات النظير (Per-Peer Options)

تُكتب داخل `{ }` في سطر `CACHE PEER`:

```cfg
CACHE PEER: 10.0.0.1:21000 { csp=1 cacheex_cwcfwd=1 cacheex_rcfilter=1 }
```

### 3.1 الخيارات الأساسية

| الخيار | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `csp` | 0/1 | 0 | بروتوكول CSP (UDP cache) |
| `fwd` | 0/1 | 0 | إعادة التوجيه للنظير |
| `cacheex_mode` | 1/2/3 | — | وضع cacheex (1=push, 2=request, 3=both) |

### 3.2 خيارات R17 (oscam compat) — OFF افتراضياً

| الخيار | النوع | الافتراضي | الوصف |
|---|---|---|---|
| `cacheex_cwcfwd` | 0/1 | 0 | تمرير `cwc_cycletime` حرفياً (لا نُفسِّره) |
| `cacheex_rcfilter` | 0/1 | 0 | قبول `rc=E_FOUND` فقط (موثَّق أدناه) |
| `cacheex_pushfilter` | 0/1 | 0 | تخزين جدول push-filter للنظير واحترامه |
| `cacheex_nodeloop` | 0/1 | 0 | قطرة push زارت قائمته هويتنا + تخطّي الهدف المُدرَج |

### 3.3 rc-filter (R17)

عند `cacheex_rcfilter=1`:
- `rc=0x64` (100, "not found") → **مرفوض** (لا تخزين)
- `rc=0xE4` (E_FOUND) → **مقبول** (تخزين)
- `rc` أخرى → مرفوض

**السبب:** r82a كان يخزّن أي rc (بما فيها 100)، فيصير الكاش مليئاً بـ CW "لا شيء".

### 3.4 push-filter (R17)

عند `cacheex_pushfilter=1`:
- النظير يُرسل لنا جدول 0x3C/0x82 (push filter) في المصافحة
- نحن نُخزّنه ونحترمه: لا نُرسل له push لا يطابق جدوله
- **اتجاه واحد:** نحن نحترم جدوله، وهو يحترم جدولنا (إن أرسلناه)

### 3.5 nodeloop (R17)

عند `cacheex_nodeloop=1`:
- **استقبالاً:** إذا ظهرت هويتنا في قائمة العقد الواردة → **قطرة** (loop)
- **إرسالاً:** إذا ظهر النظير في قائمة العقد التي سنُرسلها → **تخطّي** (لا نُعيد له)

---

## 4. البروفايلات `[ profile ]`

كل بلوك `[ اسم ]` يُعرّف بطاقة:

```cfg
[ mycard ]
CAID: 1884
PROVIDERS: 000000, 002008, 004008
PORT: 17000
USER: username password
# خيارات إضافية
SERVER: card.server.com
```

### 4.1 الحقول الأساسية

| الحقل | النوع | مطلوب | الوصف |
|---|---|---|---|
| `CAID` | hex (4 أرقام) | ✅ | CAID البطاقة |
| `PROVIDERS` | قائمة hex | ✅ | قائمة المزوّدين |
| `PORT` | رقم | ✅ | منفذ البروفايل (newcamd) |
| `USER` | `user pass` | ✅ | بيانات دخول العميل |
| `SERVER` | host | ❌ | خادم البطاقة (للعميل الخارجي) |

### 4.2 الحقول الإضافية

| الحقل | النوع | الوصف |
|---|---|---|
| `DCW CHECK` | ON/OFF | فحص DCW |
| `CACHE FILTER` | ON/OFF | فلترة كاش للبروفايل |
| `SERVICE FILTER` | قائمة | فلتر القنوات |

---

## 5. أمثلة إعداد شائعة

### 5.1 خادم كاش بسيط

```cfg
HTTP PORT: 5500
HTTP USER: admin
HTTP PASS: secure_pass

CACHE PORT: 21000
CACHE PEER: peer1.example.com:21000 { csp=1 }
CACHE PEER: peer2.example.com:21000 { csp=1 }

[ card1 ]
CAID: 1884
PROVIDERS: 000000
PORT: 17000
USER: user1 pass1
```

### 5.2 خادم مع oscam peers (R17+R18)

```cfg
HTTP PORT: 5500
HTTP USER: admin
HTTP PASS: secure_pass
DCW STATS: ON

CACHE PORT: 21000
CACHEEX MAXHOP_LG: 4

# oscam peer مع كل الخيارات
CACHE PEER: oscam-peer.com:21000 { 
    cacheex_mode=3 
    cacheex_cwcfwd=1 
    cacheex_rcfilter=1 
    cacheex_pushfilter=1 
    cacheex_nodeloop=1 
}

# csp peer قديم (بدون خيارات R17)
CACHE PEER: legacy-peer.com:21000 { csp=1 }

[ card1 ]
CAID: 1884
PROVIDERS: 000000, 002008
PORT: 17000
USER: user1 pass1
```

### 5.3 خادم مع CAID families (R16)

```cfg
# تعريف الأسر
CAID FAMILY HALFNULLED: 09, 0E
CAID FAMILY NOPROVCHK: 18, 09, 0B

CACHE PORT: 21000

[ nds_card ]
CAID: 09C4
PROVIDERS: 000000
PORT: 17000
USER: user1 pass1

[ nagra_card ]
CAID: 1884
PROVIDERS: 000000, 002008
PORT: 17001
USER: user2 pass2
```

### 5.4 خادم مع hardening كامل (R13+R14)

```cfg
HTTP PORT: 5500
HTTP USER: admin
HTTP PASS: secure_pass
DCW STATS: ON

# R13: Late-truth correction
CACHE LATE-TRUTH: ON

# R14: Pollution guard
PEER-TRUST-STORE: ON

CACHE PORT: 21000
CACHE PEER: trusted-peer.com:21000 { csp=1 }

[ card1 ]
CAID: 1884
PROVIDERS: 000000
PORT: 17000
USER: user1 pass1
```

---

## 6. التحقق من الإعداد

```bash
# فحص صياغة الإعداد
bin/multics-r82a-stats-x64 -C multics.cfg -t

# تشغيل مع سجل تفصيلي
bin/multics-r82a-stats-x64 -C multics.cfg -d

# التحقق من المنافذ المفتوحة
ss -ltnp | grep multics
```

---

## 7. الأسئلة الشائعة

**س: لماذا `CACHE PORT` مطلوب حتى لو لم أستخدم كاش؟**  
ج: r82a يتوقع `CACHE PORT` عند وجود `CACHE PEER`. غيابه يسبب segfault.

**س: هل يمكنني استخدام `MAXHOP` و `MAXHOP_LG` معاً؟**  
ج: نعم، آخر سطر يفوز. `MAXHOP: 2` بعد `MAXHOP_LG: 4` يُعيد stock.

**س: هل الخيارات R17 تُعطّل التوافق مع النظائر القديمة؟**  
ج: لا — كل خيار OFF افتراضياً = r82a الأصلي. النظير القديم لا يرى فرقاً.

**س: كيف أعرف أن `DCW STATS: ON` يعمل؟**  
ج: افتح `/cwlog` في المتصفح. إذا كان فارغاً = الثنائية ليست stats flavour.

---

## 8. المراجع

- **[COMPAT-ar.md](COMPAT-ar.md)** — مصفوفة التوافق مع النظائر
- **[FIELD-GUIDE-ar.md](FIELD-GUIDE-ar.md)** — الدليل الميداني للمشغّل
- **[TROUBLESHOOTING-ar.md](TROUBLESHOOTING-ar.md)** — حل المشاكل
- **`multics-sample.cfg`** — ملف إعداد نموذجي مُعلَّق
- **`multics-ready.cfg`** — ملف إعداد جاهز للإنتاج
