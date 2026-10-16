> ⚠️ **وثيقة تاريخية (عهد R22–R24، حتى 2026-10-15).** تشرح حالة ذلك العهد وبعض أنظمته كودٌ ميت لا يُبنى اليوم، فلا تتّبعها كدليل حالي. المرجع الحيّ: `../CHANGELOG.md` و`docs/FIELD-GUIDE-ar.md` وتقارير R26–R32. أُبقيت كما هي سجلًّا تاريخيًّا.

# حل المشاكل — MultiCS r82a (2026-10-14)

دليل شامل للمشاكل الشائعة وحلولها. كل مشكلة موثَّقة بالأعراض، السبب، والحل.

---

## 1. مشاكل البدء

### 1.1 Segmentation Fault عند البدء

**الأعراض:**
```
Segmentation fault (core dumped)
```

**الأسباب المحتملة:**

1. **`CACHE PEER` دون `CACHE PORT`:**
   ```cfg
   # خاطئ — يسبب segfault
   CACHE PEER: 10.0.0.1:21000 { csp=1 }
   
   # صحيح
   CACHE PORT: 21000
   CACHE PEER: 10.0.0.1:21000 { csp=1 }
   ```

2. **منفذ مشغول:**
   ```bash
   ss -ltnp | grep :5500
   # إذا ظهر شيء، غيّر المنفذ في الإعداد
   ```

3. **ملف إعداد تالف:**
   ```bash
   # افحص الصياغة
   bin/multics-r82a-stats-x64 -C multics.cfg -t
   ```

**الحل:**
- أضف `CACHE PORT` قبل أي `CACHE PEER`.
- تأكد من أن المنافذ غير مشغولة.
- افحص ملف الإعداد مع `-t`.

---

### 1.2 "no profile configured"

**الأعراض:**
```
no profile configured
exiting...
```

**السبب:** ملف الإعداد لا يحتوي على أي بلوك `[ profile ]`.

**الحل:**
```cfg
# أضف على الأقل بروفايل واحد
[ card1 ]
CAID: 1884
PROVIDERS: 000000
PORT: 17000
USER: user1 pass1
```

---

### 1.3 "cannot bind to port"

**الأعراض:**
```
cannot bind to port 5500: Address already in use
```

**السبب:** المنفذ مستخدم من عملية أخرى.

**الحل:**
```bash
# ابحث عن العملية
sudo ss -ltnp | grep :5500
# أو
sudo lsof -i :5500

# أوقف العملية أو غيّر المنفذ
# في multics.cfg:
HTTP PORT: 5501
```

---

## 2. مشاكل الشبكة

### 2.1 النظراء لا يتصلون

**الأعراض:**
- `/cachepeers` يظهر النظراء كـ "disconnected"
- لا توجد push/relay في `CACHEEX`

**الأسباب المحتملة:**

1. **Firewall يمنع الاتصال:**
   ```bash
   # على النظير البعيد
   telnet your-server.com 21000
   # إذا فشل، افتح المنفذ في firewall
   ```

2. **`CACHE PORT` خاطئ:**
   ```cfg
   # تأكد أن CACHE PORT يطابق ما يتوقعه النظير
   CACHE PORT: 21000
   ```

3. **بروتوكول خاطئ:**
   ```cfg
   # CSP يتطلب csp=1
   CACHE PEER: peer.com:21000 { csp=1 }
   
   # cacheex يتطلب cacheex_mode
   CACHE PEER: peer.com:21000 { cacheex_mode=3 }
   ```

**الحل:**
- افحص firewall على الجانبين.
- تأكد من تطابق المنافذ والبروتوكولات.
- راقب السجلات للاتصالات الواردة.

---

### 2.2 العملاء لا يتصلون

**الأعراض:**
- `/clients` فارغ
- العميل يرى "connection refused"

**الأسباب المحتملة:**

1. **منفذ البروفايل غير مفتوح:**
   ```bash
   ss -ltnp | grep :17000
   # إذا لم يظهر، البروفايل لم يبدأ
   ```

2. **بيانات الدخول خاطئة:**
   ```cfg
   [ card1 ]
   PORT: 17000
   USER: user1 pass1  # تأكد من التطابق مع العميل
   ```

3. **CAID/PROVIDER غير متطابق:**
   ```cfg
   # العميل يطلب 1884:000000
   # البروفايل يجب أن يحتوي عليه
   [ card1 ]
   CAID: 1884
   PROVIDERS: 000000  # وليس 002008 فقط
   ```

**الحل:**
- تأكد من أن البروفايل يبدأ بنجاح (افحص السجلات).
- طابق بيانات الدخول والـ CAID/PROVIDER مع العميل.

---

## 3. مشاكل الكاش

### 3.1 الشاشة السوداء (Black Screen)

**الأعراض:**
- العميل يتصل بنجاح
- ECM يُرسل ويُرد عليه
- لكن الصورة لا تظهر (شاشة سوداء)

**الأسباب المحتملة:**

1. **CW فايك من الكاش:**
   ```bash
   # راقب /cwlog
   curl -u admin:password http://localhost:5500/cwlog
   
   # ابحث عن "LATE-TRUTH" أو "CONVICTED"
   ```

2. **القناة "تموت بالرفض":**
   ```bash
   # راقب chanhealth
   echo "chanhealth" | nc localhost 5501 | grep dying=yes
   
   # أو /json
   curl -u admin:password http://localhost:5500/json | jq '.chanhealth[] | select(.dying==true)'
   ```

3. **رفض مفرط من الفلاتر:**
   ```bash
   # راقب CACHEEX
   echo "CACHEEX" | nc localhost 5501
   
   # ابحث عن drop عالي
   # 10.0.0.1:21000 ... drop 500 ...
   ```

**الحلول:**

**إذا كان CW فايك:**
```cfg
# فعّل R13 (late-truth)
CACHE LATE-TRUTH: ON

# فعّل R14 (pollution guard)
PEER-TRUST-STORE: ON
```

**إذا كانت القناة تموت:**
```bash
# حدد السبب من /cwlog
curl -u admin:password http://localhost:5500/cwlog | grep 1884:000000:0065

# الأسباب الشائعة:
# - profile-filter: المزوّد غير في PROVIDERS
# - cex-fake-cw: CW فايك (checksum خاطئ)
# - convicted: CW مُدان (من R13)
# - trust-store: النظير تحت الخط (من R14)
```

**إذا كان رفض مفرط:**
```cfg
# خفّف الفلاتر مؤقتاً
CACHE FILTER: OFF
DCWFILTER: OFF

# أو أضف المزوّد إلى NOPROVCHK
CAID FAMILY NOPROVCHK: 18, 09, 0B
```

---

### 3.2 الكاش لا يعمل (no cache hits)

**الأعراض:**
- `/dcwstats` يظهر `cache_hit 0`
- كل ECM يذهب إلى الخوادم

**الأسباب المحتملة:**

1. **`CACHE FILTER: ON` بدون `CACHE LATE-TRUTH`:**
   ```cfg
   # الفلتر يمنع التخزين إلا إذا كان CW "متوقع"
   # الحل: فعّل late-truth
   CACHE LATE-TRUTH: ON
   ```

2. **النظراء لا يُرسلون push:**
   ```bash
   # راقب CACHEEX
   echo "CACHEEX" | nc localhost 5501
   
   # إذا كان push=0 لكل النظراء، هم لا يُرسلون
   # تحقق من إعداداتهم
   ```

3. **`CACHE THRESHOLD` عالي جداً:**
   ```cfg
   # الافتراضي 2 — قد يكون عالياً لشبكة صغيرة
   CACHE THRESHOLD: 1
   ```

**الحل:**
- فعّل `CACHE LATE-TRUTH`.
- تأكد من أن النظراء يُرسلون push.
- خفّض `CACHE THRESHOLD`.

---

### 3.3 Drop عالي من `profile-filter`

**الأعراض:**
```
CACHEEX:
10.0.0.1:21000 ... drop 500 ... | last drop: profile-filter 1884:000000:0065
```

**السبب:** النظير يُرسل push لقنوات ليس لها بروفايل محلي (أو المزوّد غير في `PROVIDERS`).

**الحلول:**

1. **أضف البروفايل الناقص:**
   ```cfg
   [ missing_card ]
   CAID: 1884
   PROVIDERS: 000000, 002008, 004008  # أضف المزوّد الناقص
   PORT: 17002
   USER: user3 pass3
   ```

2. **أضف المزوّد إلى `NOPROVCHK`:**
   ```cfg
   CAID FAMILY NOPROVCHK: 18
   ```

3. **اطلب من النظير عدم إرسال هذه القنوات:**
   ```cfg
   # على جهة النظير
   CACHE PEER: your-server.com:21000 { cacheex_pushfilter=1 }
   ```

---

## 4. مشاكل الأداء

### 4.1 استخدام ذاكرة عالي

**الأعراض:**
```bash
ps aux | grep multics
# RSS > 100 MB
```

**الأسباب المحتملة:**

1. **جداول كاش كبيرة:**
   ```cfg
   # قلّل حجم الكاش
   CACHE THRESHOLD: 3  # يتطلب اتفاق أكثر
   ```

2. **تسريب ذاكرة (نادر):**
   ```bash
   # راقب RSS عبر الزمن
   watch -n 10 'ps -o pid,rss,cmd -p $(pgrep multics)'
   
   # إذا كان ينمو بلا توقف، قد يكون bug
   # افتح issue مع السجلات
   ```

**الحل:**
- قلّل `CACHE THRESHOLD`.
- راقب RSS — إذا كان ثابتاً بعد الاستقرار، فهو طبيعي.

---

### 4.2 بطء الاستجابة

**الأعراض:**
- ECM يأخذ >1 ثانية للرد
- `/json` بطيء

**الأسباب المحتملة:**

1. **حمل عالي:**
   ```bash
   # راقب عدد العملاء
   curl -u admin:password http://localhost:5500/json | jq '.clients | length'
   
   # إذا كان >100، الخادم تحت حمل
   ```

2. **خوادم بطيئة:**
   ```bash
   # راقب latency الخوادم
   curl -u admin:password http://localhost:5500/servers
   
   # ابحث عن latency عالي
   ```

3. **I/O bottleneck:**
   ```bash
   iostat -x 1 5
   # إذا كان %util >80%، القرص بطيء
   ```

**الحلول:**
- وزّع الحمل على خوادم متعددة.
- استبدل الخوادم البطيئة.
- استخدم SSD للسجلات.

---

## 5. مشاكل R17–R20

### 5.1 `cacheex_rcfilter` يرفض كل push

**الأعراض:**
```
CACHEEX:
10.0.0.1:21000 ... drop 100 ... | last drop: rc-filter
```

**السبب:** النظير يُرسل `rc=0x64` (not found) بدلاً من `rc=0xE4` (found).

**الحل:**
```cfg
# إذا كان النظير قديماً ولا يدعم rc صحيح، عطّل rcfilter
CACHE PEER: 10.0.0.1:21000 { cacheex_rcfilter=0 }
```

---

### 5.2 `cacheex_nodeloop` يرفض push صحيحة

**الأعراض:**
```
CACHEEX:
10.0.0.1:21000 ... loop 50 ... | last loop: nodeloop
```

**السبب:** هويتنا تظهر في قائمة العقد (loop حقيقي).

**الحل:**
- هذا سلوك صحيح — يمنع الحلقات.
- إذا كان loop عالي جداً، قد يكون هناك misconfiguration في الشبكة.
- راجع طوبولوجيا الشبكة.

---

### 5.3 `/cwlog` يُرجع HTTP 000

**الأعراض:**
```bash
curl -u admin:password http://localhost:5500/cwlog
curl: (52) Empty reply from server
```

**السبب:** تستخدم نكهة stock (لا stats).

**الحل:**
```bash
# استخدم نكهة stats
bin/multics-r82a-stats-x64 -C multics.cfg

# أو queue-stats
bin/multics-r82a-queue-stats-x64 -C multics.cfg
```

---

## 6. مشاكل البناء

### 6.1 `make test` يفشل

**الأعراض:**
```
make -C make-x64 test
# FAIL test_xxx
```

**السبب:** ملفات `x64/*.o` قديمة.

**الحل:**
```bash
cd make-x64
rm -rf x64
make test
```

---

### 6.2 `make -C tests all` يفشل

**الأعراض:**
```
make -C tests all
# build first: make -C ../make-x64
```

**السبب:** الخادم لم يُبنى بعد.

**الحل:**
```bash
cd make-x64
make  # يبني الخادم
cd ../tests
make all
```

---

### 6.3 Binary مفقود بعد `make test`

**الأعراض:**
```bash
ls make-x64/x64/test_cexvis
ls: cannot access 'make-x64/x64/test_cexvis': No such file or directory
```

**السبب:** `make test` يمسح `x64/` في النهاية.

**الحل:**
```bash
# استخدم make release-stats بدلاً من make
cd make-x64
make release-stats
cp x64/multics ../bin/multics-r82a-stats-x64
```

---

## 7. التشخيص المتقدم

### 7.1 تمكين السجل التفصيلي

```bash
bin/multics-r82a-stats-x64 -C multics.cfg -d
```

### 7.2 مراقبة السجلات حياً

```bash
tail -f multics.log | grep -E 'ERROR|WARN|drop|refused'
```

### 7.3 تحليل `/json` بـ jq

```bash
# كل القنوات "المحتضرة"
curl -s -u admin:password http://localhost:5500/json | \
  jq '.chanhealth[] | select(.dying==true)'

# كل drops مع الأسباب
curl -s -u admin:password http://localhost:5500/json | \
  jq '.cacheex[] | {peer, drop, last_reason}' | \
  grep -A 2 'last_reason.*profile-filter'

# إجمالي push/relay/drop
curl -s -u admin:password http://localhost:5500/json | \
  jq '[.cacheex[] | {push, relay, drop}] | map(to_entries) | flatten | group_by(.key) | map({key: .[0].key, total: map(.value) | add})'
```

### 7.4 مقارنة telenet و JSON

```bash
# Telnet
echo "CACHEEX" | nc localhost 5501 > /tmp/telnet.txt

# JSON
curl -s -u admin:password http://localhost:5500/json | \
  jq -r '.cacheex[] | "\(.peer) \(.origin) push \(.push) relay \(.relay) drop \(.drop) filter \(.filter) loop \(.loop)"' \
  > /tmp/json.txt

# قارن
diff /tmp/telnet.txt /tmp/json.txt
```

---

## 8. الأسئلة الشائعة

**س: لماذا لا أرى `/cwlog` في نكهة stock؟**  
ج: `/cwlog` يتطلب `#ifdef MCS_DCSTATS` — استخدم نكهة stats.

**س: هل `CACHE LATE-TRUTH: ON` يكسر التوافق مع النظائر القديمة؟**  
ج: لا — هو opt-in محلي، لا يؤثر على السلك.

**س: لماذا `profile-filter` يظهر كثيراً في drops؟**  
ج: النظير يُرسل قنوات ليس لها بروفايل محلي. أضف البروفايل أو المزوّد.

**س: هل يمكنني تشغيل نسختين من multics على نفس الجهاز؟**  
ج: نعم، بمنافذ مختلفة في كل ملف إعداد.

**س: لماذا `CACHEEX MAXHOP_LG: 4` لا يعمل؟**  
ج: تأكد أنه **بعد** `CACHEEX MAXHOP: 2` — آخر سطر يفوز.

---

## 9. المراجع

- **[DEPLOYMENT-ar.md](DEPLOYMENT-ar.md)** — دليل النشر
- **[CONFIG-REFERENCE-ar.md](CONFIG-REFERENCE-ar.md)** — مرجع الإعداد
- **[API-REFERENCE-ar.md](API-REFERENCE-ar.md)** — مرجع API
- **[FIELD-GUIDE-ar.md](FIELD-GUIDE-ar.md)** — الدليل الميداني
- **[BLACKSCREEN.md](BLACKSCREEN.md)** — أسباب الشاشة السوداء الخمسة
- **[COMPAT-ar.md](COMPAT-ar.md)** — مصفوفة التوافق
