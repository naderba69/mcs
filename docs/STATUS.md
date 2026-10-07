# الحالة الحالية — MultiCS r82a (x86_64)

**الحالة: مكتمل — DoD §0 مُشارَطة بنداً بنداً في `REPORT-4.5-ar.md` (2026-09-28). فُتح التجميد مرة واحدة لنفس اليوم لإغلاق 3.8b (آخر عيب مفتوح) وأُغلق مجدداً — D53.**

آخر تحديث: 2026-10-01 (R12 — إغلاق تداخل ردود stock: الرفض بحقّ والإدانة مرفوعة، D66) · أداة التحقق: gcc 14.2.0 / GNU Make 4.4.1 / valgrind 3.24.0

> **تحديث R12 (2026-10-01، D66):** أُغلق آخر بند في الطابور — عيب stock
> المكتشف في R4: تحت `DCWFILTER CYCLE` مسلّحة، ردّ النصف الخطأ المرفوض
> كان يوسم عقدة بايتات المفتاح `DCW_ERROR` فيبتلع الخروج المبكر ردّ
> النظير الأمين اللاحق على البايتات نفسها **صمتاً** (لا عدّاد ولا سطر)
> ويجوع العميل إلى مهلة صفرية. أعيد إنتاجه حياً قبل الإصلاح (أدلة
> `docs/interference-R12/`)، والقرار المستقل: **تناقض الدورة حكم على
> الإعلان لا على البايتات** — الرفض والعدّ والسلم باقية، الوسم مرفوع؛
> إدانات البنية والإجماع تبقى (أحكام بايتات). بلا راية — إصلاح عيب.
> الهدف الحي الجديد **`ri`**: الفخ نفسه (رفض + تسليم معاً) على stats
> وعلى dev؛ `make test` 1352؛ **الجناح 449 ok**. البصمات: stats
> `77034adf…` (= `multics.x64` المُسلَّم)، stock `e1ef16ac…`، queue
> `53d94ef4…`، queue-stats `1387786e…`، dev `f486fa89…`. **الطابور
> فارغ الآن.** التفصيل في `REPORT-R12-ar.md`.
>
> **تحديث R11 (2026-10-01، D65):** صفحة **`/cwlog`** — كل مفتاح مرّ من
> الكاش بحكمه الفردي: القناة، النظير، الحكم (delivered/refused/held/
> stored) ملوّناً، السبب (أسماء العدّادات نفسها + أكواد الحلقة: حجز/
> محكوم عليه/تحت العتبة/فلتر الباقة)، وبايتات المفتاح hex — الأحدث
> أولاً، 100 حكم بالذاكرة، خلف باب R7 وعربي/إنجليزي. قسم `cwlog` في
> `/json` بنفس الشكل في كل النكهات. التغذية مع `DCW STATS` ذاته —
> **لا مفتاح إعداد جديد**، و`cwlog.o` حيث العدّادات فقط. وحدة 37/37
> (تثبت تطابق الأسماء مع العدّادات)، هدف `vl` الحي 4/4، `make test`
> **1352 ok / 31 مجموعة**، الجناح أخضر. أفخاخ الجولة أثقل ما وُثق:
> ترقيع ضائع عطّل varargs فماتت الصفحة حتماً عند 5799 بايتاً (ASan
> وgdb أمّرانه؛ أمسكه البتر البايتي)، وعنوان هشّ في /json عاقبه -O2.
> البصمات: stats `dd29a8fd…` (= `multics.x64` المُسلَّم)، stock
> `a4d40497…`، queue `de797e1d…`، queue-stats `21bb800b…`، dev
> `99e2ed4c…`. التفصيل في `REPORT-R11-ar.md`.
>
> **تحديث R10 (2026-10-01، D64):** ثنائيات البوردات arm/mipsel/sh4
> أُعيد بناؤها من الشجرة الحالية (كانت من عهد 3.19 بوحدات العشرين
> الملف الأصلي فقط): الآن تحمل فرملة الدخول والسلّم و`/json` ومحرك
> الإجماع والسكين — والتحقق بتطابق نصوص `[CONSENSUS]` الأربعة مع
> مرجع x64، و**أول تنفيذ حي متقاطع في تاريخ المشروع: arm تحت qemu-arm
> أجاب HTTP 200**. حزمة الجاهز بيومها (ثنائيا x64 الحاليان + إعداد
> بكتلة حمايات معلّقة جديدة صفر سلوك + وثائق حالية) ودخانها الحي
> موثق (استخراج ← 200 ← dcwstats ON ← stop)؛ و`multics-project.zip`
> صار «مانيفستاً مجسَّداً» يتحقق عن نفسه (329/329 OK داخل المفكوك).
> x64 كلها والثنائي المُسلَّم باقية. التفصيل في `REPORT-R10-ar.md`.
>
> **تحديث R9 (2026-10-01، D63):** «انهيار test_peerrep تسلسلياً بعد
> test_monjson» الذي علّق الوحدة build-only منذ R6 كان تشخيصاً شعبياً
> خاطئاً — عمليتان منفصلتان لا تتشاركان ذاكرة. الجذر المثبت بـASan:
> `fopen` عارية على `/tmp/pr-unit` غير الموجود (مجلد بيئة قديم كانت
> يوجد إثره «نجاحاً منفرداً») وأول fprintf يهدم على مجرى NULL.
> الإصلاح اختباري: mkdir + `xopen()`، وسطر التشغيل عاد إلى `make test`
> بعد `test_monjson` كسلسلة انحدار دائمة. لا تغيير في `src/` —
> البصمات الخمس لإصدارات R8 باقية والثنائي المُسلَّم كما هو.
> `make test` exit 0: **1315 ok / 30 مجموعة**. التفصيل في
> `REPORT-R9-ar.md`.
>
> **تحديث R8 (2026-09-30، D62):** محرك إجماع الكاش خلف `CACHE CONSENSUS`
> (افتراضي OFF): صوت مميز لكل نظير (فيض إعادة الدفع = صوت واحد)، أول كلمة
> تُحجز نافذة إثبات (افتراضي 400 ms) — شاهد ثانٍ يسلّم فوراً، ويتيمة تُطلق
> بالنافذة (توافر أولاً؛ الخوادم لا تمسّها)؛ التعارض يُحسم بالوزن
> (distrust = صفر) والخاسر الموزون يُعدّ في `consensus-mismatch` (السبب
> العاشر) ويُدان على سلّم R4؛ الخسارة التوقيتية لا تُدين أحداً. `cn` 4/4
> ×3؛ الوحدة 25/25؛ الجناح أخضر؛ jm 8/8 ×2. البصمات: stats
> `59ea3fd1…` (= `multics.x64` المُسلَّم)، stock `8cb1db15…`، queue
> `54cb4abc…`، queue-stats `c35548a0…`، dev `08a46f59…`. وقبل إغلاق
> الجولة: **soak 1200 ث على الثنائي المُسلَّم نفسه PASS** (1202 ث، 382
> ECM، صفر إسقاط اتصال، RSS 1652 kB ثابتاً من أساس الدقيقتين إلى
> النهاية — صفر نمو؛ العدّادات متطابقة تيلنت/ويب)، و`MANIFEST-md5`
> أعيد بتثبيت **332 مدخلاً كلها OK** بعد حذف المدخل الذاتي المستحيل
> التحقق (الملف لا يستطيع حمل md5 نفسه). التفصيل في
> `REPORT-R8-ar.md`.
>
> **تحديث R7 (2026-09-30، D61):** السكين الجديدة — CSS داخلي مضمّن في كل
> صفحة (صفر موارد خارجية: معاينة معزولة = إنترنت مقطوع = نفس المنظر)،
> تجاوب هاتف (viewport، جداول قابلة للسحب ≤760px)، عربي/RTL حقيقي
> (`?lang=ar` وكوكي MCSLANG وقائمة مترجمة)، وجلسات بتوكن 256-بت على الباب
> نفسه: HttpOnly/SameSite=Strict، خمول 30 دقيقة وعمر مطلق 12 ساعة، 16 خانة بالذاكرة
> فقط (إعادة التشغيل تُخرج الجميع عمداً)، و`/logout` يبطل توكن صاحبه فقط.
> بوابات R3 لم تتحرك (القائمة والخُفاض و401). الكتابات الثلاث الجديدة
> (`http_replyok_write`/`http_html_write`/`http_style_write`) إلزامية
> لأي صفحة جديدة. الجناح exit 0 (439 ok)؛ `sk` 10/10 ×3؛ الوحدات 1257 ok.
> البصمات: stats `7ad6e449…` (= `multics.x64` المُسلَّم)، stock `300d3373…`،
> queue `1c62e782…`، queue-stats `6b47ba4a…`، dev `2ac2cbb1…`. التفصيل في
> `REPORT-R7-ar.md`.
>
> **تحديث R6 (2026-09-30، D60):** طابور الكاش المحدود خلف راية `CACHE_QUEUE`
> (افتراضي OFF، صفر مفاتيح إعداد جديدة): حلقة SPSC 256×512، المنتج = خيط
> الاستقبال (recvfrom → cq_push بلا أقفال)، المستهلك = `cache_recvmsg` غير
> الممسة بنبضة 10 ms مقيسة (500 µs = 9× CPU؛ التكيّفي أسوأ؛ 10 ms = خمول).
> A/B مزدوج بنظراء خام: 100/100 للنكهتين، صفر إسقاطات، +20 µs/داتاجرام.
> الجناح الكامل على queue-stats: exit 0 (425 ok، 0 FAIL، 3 فحوص نكهة)؛
> jm 8/8 ×3؛ الوحدات: cachequeue 23/23 وloginthrottle 25/25 أُدرج بالتسلسل
> (peerrep بناء فقط — تلوث تسلسلي موثق). الافتراضي `ffba2abb…` ×8؛ إصدار
> stats أعيد وبطابق بايتي مع R5 `f3046224…`؛ queue `c08556f9…` وqueue-stats
> `e1607001…`. التفصيل في `REPORT-R6-ar.md`.
>
> **تحديث R3 (2026-09-29، D57):** خُفاض الدخول — `HTTP/TELNET LOGIN
> DELAY` (تأخير تصاعدي ×2 حتى 8s لكل عنوان، تصفّره الدخول الصحيح) و
> `HTTP/TELNET LOGIN ALLOW` (قائمة سماح دقيقة تغلق الغريب قبل أي
> اعتماد). الهدف الحي `au` 16/16؛ الوحدات 31/31 (1184 ok)؛ الجناح exit 0؛
> soak PASS. البصمات: stats `937c4f1b…`، stock `27276ad9…`. التفصيل في
> `REPORT-R3-ar.md`.
>
> **تحديث R2 (2026-09-29، D56):** رفض الدورة المعدود — `DCWFILTER CYCLE`
> (افتراض OFF، ثلاثية الحالة لكل باقة) تحسب رفض stock الصامت المتناقض مع
> اصطلاح الشطر عند دخول الكاش (clustredcache.c:1777) في dcwstats
> (`cycle-contradiction`، العدد صار 9) وتطبع سطر `[CYCLE GATE]` واحداً
> بالسبب. الهدف الحي `cy` 8/8؛ الوحدات 31/31؛ `make -C tests all` exit 0؛
> soak 1200s PASS. البصمات: stats `bd2618ba…`، stock `bf158e72…`. التفصيل
> في `REPORT-R2-ar.md`. `checkcycle()` يبقى ميتاً (تعديل D9 النهائي).

## ما هو مُنجَز ومُتحقَّق منه

### 1. بناء كامل يعمل
```
cd mcs/make-x64
make release          # -> ../dist/multics-r82a-stock-x64
make release-stats    # -> ../dist/multics-r82a-stats-x64
make test             # 29 مجموعة / 1112 تأكيداً: 8+19+9+19+47+50+68+56+55+172+43+48+40+54+60+26+22+28+19+40+36+38+32+12+22+27+25   (وحدات)
make -C tests all     # 35 هدفاً حيّاً: smoke، no-profile، dcwfilter، stability، ncclient،
                      #   cachecw، cwreuse، cachepref، dstruct، cfg19، purgeage،
                      #   agreement، entropy، timing، plaus، cyc، nt، cm، pt، cg، tl، ha، xs، dl، ca، cu، sp، ps، u، ds، dt، dw، xe، fl،
                      #   docrefs (rc=0، 335 ok، بلا [FAIL] حقيقي؛ docs/all313-run.log)
```

الناتجان الحاليان:

| الملف | md5 | dcwstats symbols |
|---|---|---|
| `dist/multics-r82a-stock-x64`  | `3d1f58b385a7a0eea720c3c59c3bdd29` | 0 |
| `dist/multics-r82a-stats-x64`  | `f6d29d2e8585f08537b6b645d4df6666` | 2 |

> الأرقام أعلاه تتغيّر مع كل رقعة. `CHANGELOG.md` يحتفظ بأرقام اللحظة التي
> كُتب فيها كل مدخل — لا تعدّلها هناك، فهي سجل تاريخي.

كلاهما يعمل:

```
$ ./dist/multics-r82a-stock-x64 -h
Multi CardServer r82 - by evileyes (http://www.infosat.org)
```

735,544 بايت، مربوط بـ `libc.so.6` فقط (بلا تبعيات خارجية).
نسخ دائمة في `mcs/bin/` بجانب `dist/` — مجلد `dist/` لا يُحفظ ضمن لقطات
مساحة العمل ويتعيد بناؤه بـ `make release` / `make release-stats`.
دفتر إحياء التجميد من العطب (مُدرَّب فعلياً بتاريخ اليوم):
`docs/RUNBOOK-cold-restore-ar.md`.
20 كائن مُصرَّف، 0 أخطاء.

### 2. إصلاحات البناء (7) — `BUILD-PATCHES.md`
r82a لم تكن تُبنى بمترجِم حديث. الأسباب الحقيقية المصحّحة:
`config.c:2329 strptime` · `ecmdata.c:206 malloc` · `ecmdata.c:264 MD5` ·
قاعدة `%.o: ../%.c` المكسورة · مسارات التبعيات · مجلد الإخراج · `-fcommon`.

### 3. عدّادات تشخيص الرفض (المرحلة 4) — **مُنجَزة**
* `src/dcwstats.h` — الترويسة (تصريحات + أسماء الأسباب).
* `src/dcw.c` — `acceptDCW` مُجهَّزة بعدّادات **خلف `#ifdef MCS_DCWSTATS`**.
* `make-x64/test_dcwstats.c` — 19 اختباراً.

**الضمان الأهم مُثبت بالاختبار:** القرار مطابق لـ r82a غير المعدَّل لكل
المتجهات.

```
-- 1. equivalence with unmodified r82a --
  [ ok ] valid CSA-checksum CW      = 1
  [ ok ] all-zero CW                = 0
  [ ok ] corrupted-checksum CW      = 0
  [ ok ] XOR-0xF0 fake CW           = 0
  [ ok ] three-equal-bytes CW       = 0
  [ ok ] NDS half-nulled CW         = 1
```

والعدّادات تُصنّف سبب الرفض بشكل صحيح:

```
-- 4. attribution when enabled --
  [ ok ] checksum rejections = 2      (CW فاسدة + XOR-0xF0)
  [ ok ] null rejections     = 1
  [ ok ] repeat rejections   = 1
  [ ok ] accepted            = 2
```

وبلا `-DMCS_DCWSTATS` لا يُلمس أي عدّاد ولا يتغيّر الباينري:

```
-- 3. counters are OFF by default (stock behaviour) --
  [ ok ] no counter touched while disabled = 0
```

### 4. التشخيص — `BLACKSCREEN.md`
خمسة أسباب موثّقة بأرقام الأسطر، مرتّبة حسب الأولوية لحالة
«proxy + كاش، CAID 1884/NLS».

## ما لم يُنجَز بعد

| البند | الحالة |
|---|---|
| توصيل العدّادات بـ `telnet` (`dcwstats`, `dcwstats reset`) | **مُصلَح — TASK 3.11** (قراءة ON/OFF وسطر لكل سبب؛ `reset` يصفّر فعلاً؛ كلمة ثانية خاطئة لا تصفّر؛ stock يجيب مرة ويسمّي الإصدار — D41/§29) |
| توصيل العدّادات بصفحة ويب | **مُصلَح — TASK 3.12** (قسم في الصفحة الرئيسية داخل الجزء المتجدد؛ الأسماء نفسها كـtelnet بعد `html_esc`؛ الأرقام تطابق؛ stock يرسم الصفحة ويسمّي الإصدار بلا جدول — D42/§30) |
| خيار `DCW STATS: ON` في `config.c` | **مُصلَح — TASK 3.10** (كلمة فرعية داخل معالج `DCW` القائم، لا فرع جديد يبتلع `DCW TIMEOUT`؛ إعادة الضبط مع كل قراءة بما فيها إعادة القراءة؛ stock يحلّل السطر ويحذّر مرة أنه بلا عدّادات — D40/§28) |
| إحياء `checkcycle()` (السبب 5) | **مُحسَم نهائياً — TASK 4.4 (2026-09-28)**: يبقى معطلاً إلى الأبد في هذه الشجرة؛ العيوب البنيوية الثلاثة مثبتة بالتحقق، والظاهرة مُطبَّقة حيّاً في مسارَي ECM والكاش، ومرصودة قياساً بـ2.5 — تعديل D9 الختامي |
| إصلاح تجميد `cardsids_update` عند `-100` (السبب 3) | **مُصلَح — TASK 3.13** (الأرضية سقف لا قفل: فشل إضافي لا ينزل تحت `-100`، ونجاح واحد يعيد 0؛ السقف `+100` لم يُمس؛ مواضع العقوبة كما كانت — D43/§31) |
| خيارات `DCW FILTER *` لكل profile (السببان 1 و2) | **مُصلَح — TASK 3.15** (سطر داخل البروفايل يتجاوز بوابة واحدة: 1 إجبار تشغيل، 2 إجبار إيقاف؛ بلا سطر يبقى العامّ وكلاهما ON؛ المفتاح الفارغ والقائمة لا يُتجاوزان — `pf`) |
| تفعيل قائمة `BAD-DCW` (السبب 4) | **مُصلَح — TASK 3.14** (مفتاح في القائمة لا يُسلَّم ويُعدّ `bad-dcw-list`؛ خارجه يمر؛ بلا سطر القائمة مطفأة كما كانت؛ إزالة السطر تمسح القائمة — D44/§32) |
| فيض تحليل `USER` في `config.c` إلى `user[64]` | **مُصلَح — TASK 3.5** (الفرع الجديد للـ newcamd؛ الوجهان باسم سطر الإعداد) |
| خارج عن السطر في `parse_str`/`parse_name` (`str[255]` + رمز ≥255) | **مُصلَح — TASK 3.7** (السقف 254 في الدوال الست كلها؛ القضّة: 8 كتابات ASan قبل الإصلاح، صفر بعده) |
| فروع USER/PASS الإحدى عشرة الحيّة في `config.c` (بنية `[64]` نفسها) | **مُصلَح — TASK 3.8** (عبر `cfgfield.h`: نسخة محدودة عند حد الحقل + تحذير واحد بصيغة البيت؛ القضّة الحيّة قبل الإصلاح: http 401 وtelnet FAIL للزوج المقتطع، بعده 200 وprompt — D38/§26) |
| `CACHE_FLAG_SENDPIPE` لا يُمسح أبداً | **مُصلَح — TASK 3.6** (يُمسَح لحظتَي وفاة المنتظر: SUCCESS في `ecm_setdcwdata` وFAILED في `ecm_faileddcw`؛ ومواضع التسليح ترفض منتظراً ميتاً) |
| إطار `exml[5000]` في `xmlescape` | **مُصلَح — TASK 3.9** (الإطار بحدّ الخلية `exml[2048]` — كل المستدعين `char[N][2048]`؛ القضّة بـASan مباشرة على الدالة الإنتاجية: كتابة خارج الإطار قبل الإصلاح، صفر بعده؛ وصفوف profiles تعلّم escaping الخاص بـ3.2 — D39/§27) |
| منصات غير x64 (MIPSel/SH4/ARM) | **مُغلقة كبناء — TASK 3.19** (ثلاثة ثنائيات في `bin/`؛ لم تُشغَّل؛ مسارات `/opt` في Makefile ما زالت غائبة — D48) |

## ملاحظات أمانة

* المصدر العام **r82a** فقط. الباينري المنشور r107، والفجوة 25 إصداراً
  غير متاحة كمصدر. أي تحسين هنا هو على r82a.
* `dcwfilter.c` ما زال غير مُصرَّف. حُسم في TASK 3.14: الفحص يعيش في `dcw.c` (الملف المُصرَّف)، لا بنقل الملف الآخر إلى OBJECTS.
* `-fpack-struct` مُبقًى عمداً (يغيّر تخطيط الشبكة لو حُذف).
* تحذيرات `-Waddress-of-packed-member` في `main.c:1290-1319` ما زالت قائمة؛
  لم تُصلح لأنها تتطلب إعادة تصميم `struct` وليس ترقيعاً.

---

## Runtime verification — the honest boundary (2026-09-23)

Unit tests cover the logic of 1.1–1.9, 2.1, 2.2, 2.3 and 2.4 completely (18 suites, 626
assertions, all against the real headers). Runtime coverage is split:

| proven at runtime | not proven at runtime |
|---|---|
| both binaries start, bind, serve HTTP 200 | that any detector ever fires |
| startup guard refuses an empty config | that a poisoned CW is purged |
| `DCWFILTER`, `RETRY-WINDOW`, `SOFT-FAIL-WINDOW` parse | that rotation re-routes |
| stop / restart / port release | that the reuse proof triggers |
| no segfault under either build | |

**Why the right-hand column is empty.** Every protocol that carries a control word
is encrypted (CCCam, CS378X, Newcamd/DES via `des_login_key_get()`,
`srv-newcamd.c:78,190`) or requires real client behaviour. The unencrypted UDP
cache path cannot help: D11 established that its `TYPE_REPLY` carries no `ecmd5`,
so no reuse proof is possible over it.

**Update — the client now exists** (`tests/ncclient.c`), built by linking the
server's own `des.c`/`msg-newcamd.c`/`md5.c` so the framing and cipher match by
construction. It completes the full Newcamd handshake and receives `LOGIN_ACK`,
and the server logs the connection. So the crypto and framing are proven end to
end over a real socket.

**What is still missing for runtime proof of 1.2/1.3/1.5/1.6:**
1. the client sends an ECM, but **the server receives no message at all after
   login** — and it is not the ECM's fault. Measured with `-n` (`flag_debugnet`):
   the server logs `receive data 53` for the login, `send data 15` for the
   LOGIN_ACK, and then no second `receive data`, ever. Sending a 3-byte
   `MSG_KEEPALIVE` instead of an ECM produces the identical log, so nothing about
   the ECM is implicated. A probe write from the client still succeeds and
   `SO_ERROR` is 0, so the client is not closing.
   
   Eliminated by test: a login-only run leaves the connection open (so the login
   and session key are right); `SID: 0064` changes nothing; a local roundtrip
   through the server's own `des_encrypt`/`des_decrypt` shows a well-formed
   message decrypts and passes every `cs_peekmsg` check (so framing and key are
   right); an idle logged-in socket is kept (so it is not idleness).
   
   **Corrected:** `cs_peekmsg()` returns 0 for several distinct reasons, including
   a *failed `des_decrypt`*, because a negative return satisfies its
   `if (len < 15) return 0;` test (`msg-newcamd.c:237`). So the server's
   `read failed 0` is ambiguous between "peer closed" and "decryption failed", and
   an earlier note here read it as the former without checking.
   
   The suspect is the `EPOLL_NEWCAMD` registration path
   (`srv-newcamd.c:944-951` and `:921`). This is stock r82a behaviour, not a
   regression from this project.
2. there is no fake card server, so MultiCS has nothing to decode and no control
   word ever flows.

Until both exist, 1.2, 1.3, 1.5 and 1.6 remain verified by unit test and by
inspection of the wired call sites — and should be described that way, not as
"working".

## Two upstream facts found while building the harness

- **There is no SIGHUP handler anywhere in r82a.** Verified by content, not by
  memory: `grep -rn SIGHUP src/*.c src/*.h` returns four hits —
  `config.c:248`, `main.c:78`, `main.c:209` and `dcwstruct.h:207` — and **all
  four are comments this project added** (the last two arrived with TASK 1.8,
  which is why the count moved from two to four; the claim was re-run, not
  assumed).
  Neither is upstream's, and neither assumes a handler exists; they say the
  opposite, that a default is re-assigned on every load so a reload *would* pick
  it up if one were ever installed. `install_handler()` (`main.c:2581-2601`)
  registers no SIGHUP case. Hot-reload is unimplemented upstream, not merely
  untested here. An earlier version of this note cited a `main.c:75` comment as
  upstream's; there is no such comment, and the line it pointed at is now one of
  ours.
- **`SIGTERM` goes to the crash handler** (`install_handler()`, `main.c:2581-2601`),
  so a normal stop writes a backtrace and exits 1. That is the real explanation for
  the old note that `timeout` does not reliably stop this server.

## Where the runtime-proof work stands, stated plainly (updated 2026-09-23)

**Phase 2 has started: TASK 2.1, the agreement ledger, is delivered.** Two
independent sources answering for the same ECM are compared; a disagreement is
recorded and, on its own, does nothing at all. Only when the client also comes
back too soon does it become GR3's definitive proof, and even then the accusation
lands on the source whose key the client was holding — never on the source that
merely disagreed (GR1). `make -C tests agreement` proves both halves live
(13 assertions, ports 16000-16004/16010): phase 1 shows a real dispute producing
no accusation, no mark and no score change while the stats line records
`1 dispute, 0 proof`; phase 2 adds the client failure and gets exactly one
mismatch line naming the accused and the dissenter, with the accused compared
against the retry line in the same log rather than hardcoded, because the two
peers race and either may deliver first. Both mutations tried were caught: accuse
the dissenter → the GR1 assertion fails; act on a disagreement alone → phase 1
fails on two assertions.

**Update 2026-09-24 — TASK 2.2 (entropy/collision forensics) is delivered.** The
suite is now **16 unit suites / 502 assertions**, and the live `all` target is
green with `entropy` inside it. `make -C tests entropy` (ports 16100-16104/16110,
**12 checks**) proves the new layer end to end against a real cache peer: phase 1
serves an honest key and demands *silence* (no entropy line, no collision line,
`0 near (same)` in the stats summary); phase 2 has the same peer serve a key that
is **3 bits** from the first one for a different service, still passing
`checksumDCW()`, and demands exactly one `CW COLLISION` line that names the same
source, the measured 3-bit distance, both keys, and `1 near (same)` in the summary
— with no `CW ENTROPY` line, because the two halves are independent. Mutation
drill: killing the `NEAR` verdict fails 4 live checks *and* 4 unit assertions;
killing `LOWDIV` fails 2 unit assertions and, by design, nothing live (the target's
derived key has 16 distinct values so that the two verdicts cannot confound each
other) — a gap stated, not hidden: both verdicts share one call and one log/trust
path, and that path is what `NEAR` proves live. Three harness defects were found
and fixed on the way, the most expensive being that `ncclient.c`'s `$5` is the
Newcamd **DES key**, not an ECM: varying it to mean "a different ECM" produced a
`wrong des key` disconnect that read like a product defect for several turns.

**Delivered and verified: Phase 1 is complete.** Fourteen units of work (1.0,
1.0b, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, **1.9**, 1.10a/b/c, 1.11a) are
implemented, wired, and covered by 14 unit suites / **392** assertions that
exercise the real headers, plus a live harness that boots both binaries, serves
HTTP, parses the new options, drives the server's ECM path with a real Newcamd
client, and delivers a real control word through a real cache peer.
`make -C tests all` runs **95 live checks, all green**. The build is clean at
`-Wall -Wextra -O2`: 114 warnings under the project's usual suppression list and
189 without it, none of them from a file this project added.

**TASK 1.9 is delivered and proven at runtime** (`make -C tests cfg19`, 10
assertions): with `BAD-CW-LIMIT: 3`, three confirmed bad-CW events from one cache
peer produce two "counted, below the limit" lines and exactly one avoidance line,
and the client is still answered throughout — the tolerance never cost a
delivery. `STATS-WINDOW: 2000` emits summary lines that report the limit actually
in force. `make -C tests purgeage` (8 assertions) proves the other half:
`SERVICE-BLACKLIST-TIME: 3000` lifts a reuse-proof mark once, naming the channel
and the source the mark line named, while the same run with the option absent
leaves the same mark standing.

**One coverage gap, stated plainly.** `BAD-CW-LIMIT` changes *routing* only
through `srvtab_arrange()` in `loadbalance.c`, i.e. when choosing a card server.
The live harness has mock clients and a mock cache peer but no mock Newcamd
*server*, so no live target can show a source being skipped. What is proven live
is the counting and the log discipline; the predicate is proven by the unit
suite. The gap was found by a mutation that the harness did not catch, and it is
not claimed otherwise. Building a mock card server is the work that would close
it.

**The ECM path now runs end to end.** A real client logs in, sends an ECM, and the
server answers it. With no card server configured the answer is `decode-failed`
(MSG_CW, type 0x80, 3 bytes) — which is the correct answer, and the first
complete request → process → reply cycle this project has observed. The harness
sends the same ECM twice 2 s apart so both branches of the ECM handler run:
rep 1 with `ecm == NULL` (a profile's first request), rep 2 through upstream's
existing-ECM branch. It asserts the server answered both and is still alive.

**A control word has now been delivered end to end, and four detectors have fired
at runtime.** `mcs/tests/cachepeer.c` is a minimal CSP cache peer; `make -C tests
cachecw` drives client ECM → server asks peer → peer pushes a CW → server
delivers it → client re-asks 2 s later, and asserts eight things including the
GR10 suppression (1.0b), RETRY_HARD (1.2), the `DCW_SOURCE_CACHE / PEER_CSP|1`
origin in the retry line (1.1) and the rotation record (1.6). Pointing it at an
all-zero CW makes six of them fail, so the assertions are real.

**TASK 1.3 and 1.5 are now proven at runtime too**, by `make -C tests cwreuse`: a
peer that supplies the same key for two services produces one CW-reuse proof and
one cache purge, and the same key supplied twice for *one* service produces
neither — which is the GR6 case, and the one that would blacklist a working
source if it were got wrong. Getting there also closed a real coverage hole: the
CSP UDP cache protocol was not watched by either detector, because D11 concluded
the `ecmd5` was unavailable. It was available, on the cache entry rather than on
the request. See D16.

**TASK 1.7 is shipped and proven at runtime too**, by `make -C tests cachepref`.
Two runs that differ in one config line: with `TRUSTED-CACHE-FIRST: ON` a stored
key from a peer that has fallen below the trust line is deferred 13 times on a
request that has nothing to cross-check it against, and every client still gets
answered; with it OFF the same entry is served and nothing is deferred. Putting
`return CACHEPREF_ALLOW;` in the policy and rebuilding makes the ON case fail,
so the test bites. Getting there also fixed a real defect: the trust table was
being written under two different mutexes (`prg.lockecm` from `srv-newcamd.c`,
`prg.lockcache` from the cache-exchange sites), which is no exclusion at all. It
now has a leaf lock of its own — see D17.

**TASK 1.8 is delivered and proven at runtime** (`make -C tests dstruct`, 8
assertions): the cache peer pushes a key whose two halves are byte-identical — a
shape no real key has — and the client still receives it untouched, exactly as
before, while the server logs one line naming it and attributing it to
`DCW_SOURCE_CACHE/PEER_CSP|1`. Three further pushes of the same key produce no
further line, so the repeat suppression holds in the real thread context, not
just in the unit table.

**Update 2026-09-24 — TASK 2.3 (card-server latency forensics) is delivered**,
and with it a real defect that had nothing to do with latency. `make -C tests
timing` (ports 16200/16203/16204/16210, **21 checks**) runs three phases against
a new harness, `tests/ncserver.c` — a mock Newcamd **card server** built from the
server's own crypto, which answers each ECM after a *scheduled* delay:

| المرحلة | يرد خادم البطاقة في | المطلوب |
|---|---|---|
| 1 صادقة | 55–75 م.ث (تذبذب 20 م.ث) | صمت تام: ولا سطر، و`0 fast, 0 flat` في السطر الإحصائي |
| 2 مِترونوم | 55 م.ث بالضبط | سطر FLAT + صيغته التي تنفي كونها دليلاً، و**الثقة تبقى 0** |
| 3 فورية | 0 م.ث (الأرضية 42 م.ث) | سطر FAST لكل قناة، و**الثقة 0 ← 8 مصادر** (تفصيل GR4 حيّاً) |

`CWT_FAST` وحدها تدخل `CWT_SCOREMASK`؛ `CWT_DEGEN` تُطبع وتُعدّ ولا تُنقّط،
وتُحجب عن السطر الإحصائي كل 60 ثانية مع استمرار العدّ. جُرّبت مقاييس هذا
المسار فعلياً: ردٌّ ثابت فعلاً قِيس 30–36 م.ث، وردٌّ بمهلة 50 م.ث قِيس 51–52 —
فالنطاق «المِترونومي» كان 1 م.ث وهو **أدنى من ضجيج المسار نفسه**، وصار 5 م.ث
بالقياس لا بالتقدير (D22). الأرضية تبقى 0 افتراضياً (مُطفأة) لأن خادم Newcamd
الوسيط بردّ من ذاكرته أسرع من البطاقة **بشكل مشروع**.

**وأثناء إعادة تشغيل المجموعة الكاملة** ظهر عيب حقيقي في المنتج: ملف إعداد بلا
قسم `[ profile ]` كان يُسقط الخادم بـ SIGSEGV — `get_cache_caids()` كانت تقرأ
`cfg->cardserver->card.caid` قبل اختبار المؤشّر، و`check_config()` تناديها أولاً.
والحارس الذي وُضع لمنع ذلك كان يسابق الخيط الذي يسبّب السقوط: نومتان متساويتان
(100 م.ث في main و100 م.ث في خيط الإعداد)، فمن يستيقظ أولاً يقرّر النتيجة — 29
مرة `exit 1` ومرة `SIGSEGV` في 30 تشغيلاً. نُقل الفحص إلى مكان إنتاج بياناته
(خيط الإعداد، بعد `read_config()` مباشرة) وأُصلح المؤشّر نفسه (D23). الإثبات:
النسخة قبل الإصلاح انهارت 5/5 تحت إجبار السباق، وبعد الإصلاح `exit 1` في 100
تشغيل متتالٍ. وإعادة التحميل (SIGHUP) بلا مقطع profile لم تعد تُسقط الخادم،
وسُجّل ما تبقّى منها غير مدقَّق في `D23` بدل إصلاحه على العميان.

**Update 2026-09-24 — TASK 2.4 (plausibility scoring) is delivered**, and with it
a blind spot in the *architecture* rather than in the layer. `acceptDCW()` يسأل
سؤالاً بنعم/لا (أربعة مجاميع جمع في `checksumDCW`)، والجواب نفسه يغطّي ثلاث حالات
مختلفة: مفتاح صفري، ضجيج، ومفتاح **صحيح إلا من بايت واحد** — والأخير هو التزوير
الذي يحذّر منه سجل r107 (قلب آخر بايت بـ XOR). الطبقة الجديدة تعدّ كم مجموعة من
الأربع صحيحة، و«3 من 4» تعني بايتاً واحداً تالفاً في مفتاح سليم البنية:

| الحالة | المعنى | الأثر |
|---|---|---|
| 4/4 | الشكل المتوقّع | يُعدّ فقط، لا سطر ولا نقطة |
| 3/4 | بايت واحد تالف | **قابل للتنقيط**، وفقط كنمط |
| ≤2/4 | لا يشبه القاعدة أصلاً | يُعدّ، ولا يُنقّط أبداً |

النمط يحتاج أربعة شروط معاً: **3** مفاتيح على الأقل، في نافذة **60 ثانية**،
بنسبة **20 %** من مرور المصدر، وكلها تفشل **نفس المجموعة**. لولا شرط النسبة
لاتُّهم مصدر مزدحم شريف (1.52 % من مفاتيحه تقع على 3/4 بالمصادفة)، ولولا شرط
المجموعة نفسها لما كان الحكم عن بايت واحد بعينه. أسوأ حالة شريفة: 3.2e-5 لكل
نافذة (≈ مرة كل تسع ساعات من البثّ المتواصل)، مقابل التزوير المتواصل يُدان
عند المفتاح الثالث. `CWP_WEAK` يُعدّ ولا يُنقّط لأنه ببساطة شكل كل مفتاح شريف
عند باقة لا تستخدم قاعدة الجمع.

**والأهم:** `cache_setdcw()` (`clustredcache.c:1561`) يفحص `acceptDCW()` **قبل**
أي هوك، بينما كل هوكوك الأدلّة في هذا المشروع موضوعة **فوق** البوابة داخل
`setdcw.c` — أي أن مفتاحاً مرفوضاً قادماً من الكاش كان **غير مرئي لأي طبقة
أدلّة**: قيس فعلياً — أربعة مفاتيح مزيّفة، أربعة ردود، صفر تسليم، وسطر إحصائي
`0 full, 0 one-group`؛ الطبقة كانت عمياء لا صامتة. أُغلق ذلك لهذه الطبقة عند
البوابة نفسها وعلى مسار الرفض فقط (فالمفتاح المارّ يُعدّ مرّة واحدة حيث
يُسلَّم)، وسُجّل صراحةً أن 1.8 و2.2 ما زالتا عمياوين عن المفاتيح التي تفشل
التحقّق وتأتي من الكاش (D24) بدل إصلاحهما في الخفاء.

الهدف الحيّ `make -C tests plaus` (المنافذ 16380-16385/16390، **21 تأكيداً**)
بثلاث مراحل وثلاثة أقران على منافذ مختلفة (فالندّ يُعرَّف بعنوانه: منفذ واحد
يعني مصدراً واحداً، وهي مرحلة كانت ستنجح لسبب خاطئ): مرحلة شريفة بأربعة مفاتيح
**مختلفة** صحيحة تُسلَّم فعلاً وبلا أي سطر؛ ثم ندّ يُفسد كل مفتاح ← مفتاحان
صامتان والثالث ينتج **سطراً واحداً** يسمّي المجموعة 4 والعدد والنسبة، والثقة
0 ← 1، ولا مفتاح مزيّف يصل عميلاً، ولا proof ولا rotation (GR2: مفتاح مرفوض هو
مهلة، لا تُهمة)؛ ثم ندّ يُفسد مفتاحاً واحداً من أحد عشر (**9 %** تحت الخط) ← لا
سطر ولا نقطة، وعشرة مفاتيح صحيحة **تُسلَّم**. تدريبان بالتحوّل: قتل `CWP_SCOREMASK`
يُلتقط وحدةً وحيّاً (1+1)، وإزالة شروط العدّ/النسبة/المجموعة تُفشل **14 تأكيداً**
وثلاث فحوص حيّة — منها مرحلة الضبط، وهذا سبب وجودها. كلاهما رُجع وأثبت التطابق
بـ `diff -q`.

**Still not delivered:** nothing from the numbered Phase-2 plan — the trust
lifecycle (fade + persistence) shipped as TASK 2.10, completing the plan
after 2.8 (coarse tier) and 2.9 (cache protocol guard). What remains open is
the defect list, not a task. Closed from it so far: the HTTP auth bypass
and the trusting HTTP request parser (TASK 3.1), the raw client names in
the web interface (TASK 3.2), the logging-ring overflow
(`debugf`/`add_dbgline`, TASK 3.3 — bounded formats, bounded ring writes,
semantics pinned), the unchecked cache allocations
(`clustredcache.c`, TASK 3.4 — fourteen guards, one event dropped per
failure, bite-proven both ways by `tests/ca-bite.c` + the `ca` target),
and the config `USER` record corruption (`config.c` newcamd branch,
TASK 3.5 — both fields bounded at their own edge with a named warning;
pre-fix, a 70-char password answered `unknown user` to its own entry).
`CACHE_FLAG_SENDPIPE` never cleared (clustredcache.c/setdcw.c/th-ecm.c,
TASK 3.6 — the arm now dies with its waiter: cleared at the SUCCESS and
FAILED commits, arm sites refuse a dead waiter; pre-fix the second
client decode-failed with the key in the entry and peers got silence,
post-fix 6/6 answers and instant second-client serve, target `sp`).
`parse_str`/`parse_name` and four sibling parsers clamped at 255 and
wrote the terminator one byte past `char[255]` (parser.c, TASK 3.7 —
clamp now 254; bitten pre-fix under ASan: eight one-byte stack writes
per startup with 255-char config tokens, zero post-fix; new permanent
tool `x64/multics-asan` and live guard `ps`).
Closed since this paragraph was first written: the sibling USER/PASS
branches (TASK 3.8), the `xmlescape` frame (TASK 3.9) and `DCW STATS:
ON` (TASK 3.10 — live target `ds`). Telnet `dcwstats` / `dcwstats reset`
shipped as TASK 3.11 (live target `dt`). The home page shows the
same counters as TASK 3.12 (live target `dw`: page, refresh fragment
and telnet agree slot by slot). The `cardsids_update` −100 freeze
shipped as TASK 3.13 (live target `fl`: the production function shows
−100, stays there through 50 further failures, and one success returns
0; the profile page shows `DCW MAXFAILED` 100). The documented `BAD-DCW` list shipped as TASK 3.14 (live target `bd`:
a listed key is not delivered and `bad-dcw-list` counts 1; a key not
on the list is delivered and the counter stays 0; no line delivers
the key and logs nothing). Per-profile `DCWFILTER` shipped as TASK 3.15
(live target `pf`: the profile with checksum off received the failing
key, the profile with no line did not, and that same profile still
received a key that passes). A late cache push no longer revives a
failed ECM (TASK 3.16, live target `fs`: the in-flight push was
refused, and a push that arrives while the ECM is still waiting is
still delivered). A 1024-card peer list no longer reads one past
the array (TASK 3.17, live target `pc`: the server stored 1024
cards, delivered the listed card, and did not ask for an unlisted
one). HTTP restart still comes back without a thread stopper
(TASK 3.18, live target `rs`: the old process exits, the new one
binds the same ports). MIPSel, ARM and SH4 each have a compiled
binary (TASK 3.19: `make -C make-cross all`); they were not run, and
the x64 release of that task was unchanged. Official memory checks
are in place (TASK 4.1: `make -C make-x64 memcheck`). A blank config
line no longer reads before its buffer, and startup waits until the
config has been read. TSan still reports the old shared-state races;
those are not locked (D49). The 20-minute soak stayed up (TASK 4.2) and
the packed-member warnings left the main.c area with identical addresses
(TASK 4.3, D51). `checkcycle()` is closed for good (TASK 4.4, D9
amendment). Package أ's last defect, `parse_quotes` (3.8b), was closed
by a single dated reopening of the freeze (TASK 3.8b, D53: the copy is
bounded by the caller's buffer at all 13 sites). Nothing is open; the
backlog of PROJECT-PLAN §4 starts only on an explicit named order.
Anything the
operator reports from production remains. The maintained list is
the table above.

### The segfault that blocked all of it — and what it actually was

The crash was **not** upstream's, and the previous version of this section said it
was. That was wrong. It was code I added for TASK 1.2 at `srv-newcamd.c:490`:

```c
ECM_DATA *ecm = search_ecmdata_any(...);   /* NULL on a profile's first ECM */
if ( (cli->lastecm.hash==ecm->hash) || !isnew ) {   /* dereferences NULL */
```

Upstream does not touch `ecm` until its own `if (ecm)` guard at `:556`; the hook
sat above that guard and read through the pointer anyway. Fixed by gating on a
same-channel test instead — see `CHANGELOG.md` for the reasoning, including the
second defect on the same line (the old gate would have scored a channel *zap* as
a retry, penalising a good source, which is exactly what GR3 forbids).

**The lesson recorded here rather than in the changelog:** eleven green unit
suites did not see this, because none of them links `main.c` or `srv-newcamd.c`.
A crash on the first ECM of the first client is not an edge case — it is the
first thing any deployment does. Any hook placed in the ECM path needs a runtime
check that executes it, and that check has to be shown to fail when the bug is
put back. Both are now true of `make -C tests ncclient`.

### The client key trap (settled)

`des_login_key_get(key1, key2, len, des16)` takes the **DES key** as `key1`. The
real client calls `des_login_key_get(srv->key, passwdcrypt, ...)`
(`cli-newcamd.c:98`); the server matches with
`des_login_key_get(cs->newcamd.key, ...)` (`srv-newcamd.c:190`). Passing the
password compiles clean, logs in fine, and fails on the *next* message, because
`cs_peekmsg()` returns 0 through its `if (len < 15)` test when `des_decrypt`
rejects the checksum — producing a bare `read failed 0` that is indistinguishable
from the client having closed the socket. Six turns of blac the *next* message, because
`cs_peekmsg()` returns 0 through its `if (len < 15)` test when `des_decrypt`
rejects the checksum — producing a bare `read failed 0` that is indistinguishable
from the client having closed the socket. Six turns of black-box inference did not
find it; one temporary print of the server's own key did.

### Environment traps added this round

- `A && B && C &` backgrounds the **whole list**, so `$!` is the subshell and
  killing it leaves the server running. Three orphans accumulated this way and one
  of them kept holding the test ports, which made a later run connect to the wrong
  process. Capture `$!` in the same shell line, and verify the port is free before
  asserting anything.
- The `dist/` artifacts are `strip`ped. `nm` on them finds nothing, so the
  dcwstats symbol count has to be taken from `x64/multics` — which is what the
  `release` and `release-stats` targets already print.
- A stale hand-built `./ncclient` next to the Makefile's `.ncclient.bin` gets run
  by accident and produces output from the old code. The hand-built copy is
  deleted; only the Makefile target builds the client.

**Update 2026-09-25 — TASK 2.5 (cache key cycle/parity visibility) is delivered.**
الفئة الخامسة من المفاتيح «السليمة التي لا تفتح» كان بلا شاهد: مفتاح من **الفترة
السابقة**، أو النصف الآخر من زوج even/odd — ستة عشر بايتاً سليمة البنية، أربعة
مجاميع صحيحة، إنتروبيا عالية، بلا خلاف مع أي مصدر، وبلا تغيير SID ليراه 1.3.
البروتوكول يحمل الجواب أصلاً: طلب ECM يصرّح بالنصف المطلوب (دالة في وسم ECM
مقابل `cw1cycle` للقناة)، وقرين CSP الذي يمرّر قد يصرّح بالنصف الذي يرسله في
`buf[29]`. المهمة مربطة هذين التصريحين في **قياس** فقط:

- `src/cwcycle.h` — لكل مصدر، في نافذة 60 ثانية: ما **عُرض** (منه ما حمل علامة،
  ومنه ما **خالف** التوقّع) وما **سُلِّم** لعميل منتظر (منه ما سُلِّم رغم
  التناقض = **stale**، وما تعذّر الحكم عليه = **unverified**). جملة واحدة لكل
  مصدر في النافذة، تسمّي النصفين ونقطة المشاهدة وتقول حرفياً «لا شيء رُفض أو
  أعيد أو نُقِّط: هذا قياس لا اتهام».
- نقطتا مشاهدة عبر غلاف واحد `ccy_note()` في `main.c` تحت قفلاً ورقياً واحداً:
  العرض داخل `cache_setdcw()` (خيط الكاش، التعريف عند `clustredcache.c:1561`، الملاحظة عند `:1640`)، والتسليم
  في `ecm_setdcwdata()` بعد `clients_check_sendcw()` عند `setdcw.c:700` (خيط
  setdcw). العلامة تسافر **خارج المفتاح** — بايت واحد مُلحق برسائل الأنبوبين
  الداخليين (41/48 و34/64 بايت)، و`ecm_setdcw_marked()` هي المدخل الجديد
  الوحيد، وكل المستدعين الحاليين بقيوا على `ecm_setdcw()`.
- سطر STATS-WINDOW يزيد: `cycle offered N (M marked, K contra), handed H (S
  stale, U unverified, L lines)`.
- **لا رفض ولا نقطة ثقة إطلاقاً** (GR3: دورة معلنة دليل لا برهان) — بوابة
  Check Cycle العلوية (`clustredcache.c:1568`) بقي سلوكها كما هو، والهدف الحيّ
  يُثبت ذلك: المفتاح المتناقض يُرفض قبل أي عميل، ويُسجل مرة واحدة بالضبط، ولا
  يتحرك الثقة.

**الأدلة:** `make test` = 19 مجموعة / **681** `[ ok ]` (منها `test_cwcycle`
55/55 الجديدة). الهدف الحيّ الجديد `make -C tests cyc` (منافذ 16400-16403 و
16410): قناة تُصرّح بـ `.81` وقارئ `fwd=1` تتحكم فيه الأداة بعلامة الردّ
(`CP_CYCLE_MARK`) — الطور 1 علامات صادقة: 3 تسليمات وبلا سطر؛ الطور 2 علامة
متناقضة: رفض قبل أي عميل، **سطر واحد بالضبط** يسمّي النصفين، الثقة لا تتحرك،
`offered 5 (5 marked, 2 contra), handed 3 (0 stale, 1 line)`؛ الطور 3 بلا
علامة: رفض أيضاً، يُعدّ عرضاً بلا علامة، وبلا سطر جديد — التناقض والصمت يبقيان
شيئين مختلفين.

**Update 2026-09-25 — TASK 2.6 (negative memory, "never twice") is delivered.**
البراهين القاطعة كانت تُدين **المفتاح**، لكن كل ما يُفعل بالحكم كان موجّهاً
**للمصدر**: علامة 1.5 تُسجَّل على الأصل، والتطهير يمسّ فقط المفاتيح التي
`cwdata->peerid` هو ذاك الأصل، ولا شيء يُستشار وقت العرض إطلاقاً. النتيجة:
نفس البايتات من مُمرِّر آخر، أو من الأصل نفسه بعد انتهاء القيد، تعود
**للعميل مرة ثانية** — والبرهان الحقيقي بلا ذاكرة. 2.6 يغلق هذا:

- `src/cwneg.h` — جدول ثابت 128 خانة في .bss، مُفتّاح على **بصمة المفتاح**
  (FNV-1a 64 بت) في نطاق (CAID, PROVID). لا صلاحية زمنية: البرهان لا ينتهي،
  والجدول محدود بإخلاء LRU، والجدول الممتلئ يُخلّ دائماً ولا يرفض البرهان
  التالي أبداً.
- **ثمانية مواضع علامة** فقط — حيث تعمل البراهين أصلاً: فرع LEDGER_PROOF في
  `ledger_act` (المفتاح الذي حملَه العميل وفشل عليه)، وموقع برهان إعادة
  الاستخدام في CSP (`clustredcache.c:1872`) ومواقع cache-ex الستة. لا علامة
  من نقطة، ولا من شكل، ولا من دورة، ولا من مهلة (GR3).
- **بوابة واحدة** في `cache_setdcw()` (التعريف عند `clustredcache.c:1561`، البوابة عند `:1603`) بعد ملاحظة
  2.4 وقبل بحث القيد: المفتاح المدان لا يُخزَّن ولا يُعدّ توافقاً بين قرائن
  ولا يصل لأي عميل. كل رفض يُعدّ، وسطر إثبات واحد لكل مفتاح في 60 ثانية
  يقول مَن أثبت ومتى، وينتهي بـ«ليس نقطة: لم يُنقَّط شيء ولا عُطِّل مصدر».
- **درس النطاق (سُجِّل في D26):** أول بناء كان بنطاق (CAID, PROVID, SID)
  وفشل الهدف الحيّ بالطريقة الصحيحة — السمّ المُثبت على الخدمة 2 عبر خدمة 3
  بـ SID مختلف وسُلِّم ثانية. الهوية الآن (بصمة المفتاح، CAID, PROVID): مفرمة
  واحدة تتشارك خدماتها CW واحد، وSID يتغير مع كل zap والسمّ لا يتغير. GR6
  محفوظ بالاتجاه الصحيح: البصمة هي المميّز الأول.
- سطر STATS-WINDOW زاد: `| negative N live (M proven, H refused, L line(s))`
  — ومخزن تنسيق السطر كبر 512→1024 بايت (السطر كان يُبتَر وسط حقله).


### TASK 2.9 — حارس بروتوكول الكاش (2026-09-25)

تحت كل طبقات المرحلة الثانية تجلس حزم CSP الخام، وهناك عثوان مؤكدان على
الشجرة قبل كتابة أي سطر:

1. **قرارات من ذاكرة مكدّس غير مهيّأة:** الأرضية العالمية `received >= 2`
   فقط، وخمسة معالجات تقرأ ما بعد ذلك بلا فحص: REQUEST (buf[1..11])،
   REPLY (buf[12])، PINGREQ (buf[11..12])، PINGRPL (buf[4..5])، HELLO_ACK
   (buf[4..8]). طلب قصير ينشئ مدخل كاش مفتاحاً بهاش عشوائي؛ بينق قصير
   يرسّخ نظيراً — ومع CACHE AUTOADD ينشئ نظيراً — عند منفذ عشوائي.
2. **مرسلون أشباح:** `if (!peer) break;` يُسقط حزم المرسلين غير المعرّفين
   بلا عدّاد ولا سطر — غالبهم نظير مشروع أسئ التهيئة، وسؤال «لماذا لا
   يشارك نظيري؟» بلا جواب.

الحارس `src/cacheguard.h`: الحد الأدنى لكل نوع = الإزاحات التي يقرؤها
المعالج فعلاً (12/13/13/6/16/9؛ والأنواع المجهولة تبقى على أرضية المخزون 2
— لا سياسات مُختلَقة)؛ القصير يُسقط قبل أي تحليل مع سطر واحد لكل نوع كل 60
ثانية يسمّي النوع والطولين والمرسل ويقول «لم يُحلَّل شيء»؛ ومرسل غير معرّف
يُعَدّ ويُسمّى مرة بالإنافذ بينما معاملته لم تتغير — المخزون أسقطها قبل أي
طبقة وما زال. شريحة الإحصاء `| cache guard S short, U unconfigured`. بلا
أقفال: الخطّافات على خيط الكاش والإحصاء يقرؤها من نفس الخيط.

**ولا حاجة لما يعلنه r107:** فلتر «لا تجب إلا إذا دور الـ cw أو ثبت أنه
ليس مزيّفاً» — شجرتنا ترفض مفاتيح غير `acceptDCW()` عند **الإدخال** في
`cache_setdcw` أصلاً، وهي أصرم من الفلترة وقت الرد. تحقّق وسُجّل في D29.

**الأدلة:** `make test` = 23 مجموعة / **985** `[ ok ]` (منها
`test_cacheguard` 41/41). الهدف الحيّ `make -C tests cg` (منافذ
16640-16644/16650، نظير صادق A ودافع مظلم S): الطور الصادق صامت؛ دفعات S
تُعَدّ وتُسمّى ومعاملتها كما كانت؛ ردّ A المبتور 8 بايتات يُسقط قبل أي
تحليل بالسطر والشاهدَين؛ وردّ A التالي السليم يُسلَّم — البوابة لا تكلّف
مرسلاً جيداً شيئاً، وسطران بالضبط في كل الجولة.

### TASK 2.8 — محرّك ثقة نظراء الكاش (2026-09-25)

المرحلة الثانية بنت خمسة كواشف وبرهانين، وكلها تكتب المفتاح الخماسي
(المصدر، CAID, PROVID, SID) — التفصيل الصحيح لقرار تسليم، والعنوان الخطأ
لسؤال «من هذا الخصم؟». سمّ يدلفّ سمومه على خمسين خدمة يترك خمسين مدخلاً
بخدش في كل واحد: الشواهد موجودة ولا أحد يجمعها، والمشغّل لا يرى وقف نظير
في أي مكان، وجولة طلبات الكاش تسأل كل نظير عمياء عن الثقة.

الطبقة الخشنة الجديدة `src/trustagg.h` تجمع **نفس الأحداث** على
(المصدر، CAID) — عادة المُرسِل — بحساب trust.h نفسه حرفياً. وعندما يهبط
درجة نظير تحت خط التدخل يُكتب سطر `!!! PEER TRUST:` واحد بالضبط يسمّي
المصدر والـ CAID والعدّادات، وينتهي بـ«لا شيء يُقطع، لا عميل يُمَسّ،
والمفاتيح التي يُبقيها العملاء تستعيد الطلبات». المفتاح جديد:
`PEER-TRUST-REQUESTS` (الافتراضي **OFF**) — وحده يسلّح الفرملة: نظير تحت
الخط على CAID ما لا يُسأل عنه مدى نافذة 60 ثانية، ثم طلب واحد يختبره من
جديد. الدفعات والردود لا تُقطر أبداً، ولا شيء يُقطع، والتسليم الجيد
يعيد الدرجة صعوداً كما في trust.h تماماً.

**الأدلة:** `make test` = 22 مجموعة / **944** `[ ok ]` (منها `test_trustagg`
48/48 — تطابق الحساب مع trust.h خطوة بخطوة). الهدف الحيّ `make -C tests pt`
(منافذ 16620-16624/16630، نظيران A/B): الطور الصادق صامت، برهانا reuse على
(نظير، CAID) واحد يعبران الخط بسطر واحد يسمّي «2 proof(s)»، ثم الخدمة
السابعة: حصة B من الجولة تُقطر (عدّاد طلباته يتجمّد عند 3) وA يُسأل بدلاً منه
والعميل يأخذ مفتاحه — إقفال نظير مُسمِّم لا يكلّف عميلاً شيئاً.

### TASK 2.7 — فحص المرآة المتمّمة (2026-09-25)

الصف الرابع من مفاتيح CW المشكّلة جيداً: النصف الثاني من المفتاح هو متمّمة
النصف الأول bitwise. تحقّقنا من الحساب قبل كتابة أي سطر (المطالبة جاءت من
خطة خارجية):

- المرآة **الخالصة** تُخطئ مجموعَي المجموعتين 3 و4 بمقدار 2 بالضبط — طبقة
  الفحص ترفضها أصلاً و2.4 تعدّها ضعيفة؛ المطالبة الخارجية نُقّحت.
- المرآة **المُصلَحة** (إصلاح المجموعين المكسورين) تمرّ بكل الطبقات الهيكلية
  وتُسلَّم بصمت — هذه هي الفجوة الحقيقية.
- مسبار 50,000,000 مفتاح عشوائي صحيح المجاميع: **صفر** تحمل 4/6 أزواج
  متمّمة أو أكثر.

الطبقة الجديدة `src/cwcm.h` رصدٌ فقط: ستة أزواج بايتات حرة (بايتات الفحص
لا تُقترن أبداً)، 5/6 أو أكثر = مرآة؛ جدول 64 خانة بهوية
(نوع المصدر، معرّف المصدر، CAID) — العادة صاحبها المُرسِل وليس الخدمة (D27)؛
النمط = 3 مفاتيح مرآة و20% من نافذة 60 ثانية → سطر `CW MIRROR:` واحد
+ حدث ثقة ناعم واحد؛ المفتاح المفرد يُعَدّ ولا يُطبع ولا يُنقّط؛ لا رفض
في أي مكان — السطر ينتهي بـ"التسليم لم يُمَسّ بهذه الطبقة".

**الأدلة:** `make test` = 21 مجموعة / **896** `[ ok ]` (منها `test_cwcm`
43/43). الهدف الحيّ `make -C tests cm` (منافذ 16600-16603 و16610): المفاتيح
الصادقة صامتة، المرايا المُصلَحة **تُسلَّم** (3 → 5 → 6) و2.4 يقرؤها بشكل كامل
(صفر أسطر plausibility)، المفتاح الثالث يعبر النمط → سطر واحد بالضبط
+ الثقة 0 → 1 (ناعم)، ولا تشابك مع reuse/negative، والإحصاء
`mirror 6 keys, 3 complement-built (1 pattern)`.
**الأدلة:** `make test` = 20 مجموعة / **853** `[ ok ]` (منها `test_cwneg`
172/172). الهدف الحيّ `make -C tests nt` (منافذ 16500-16503 و16510): قارئ CSP
واحد يجيب ثلاث خدمات بنفس المفتاح — برهان على الخدمة 2 (سطر MARK مرة واحدة
بالضبط)، رفض على الخدمة 3 (سطر واحد قبل أي عميل)، التسليمات مثبتة عند اثنتين
تماماً (ما قبل البرهان فقط)، إعادة الإثبات صامتة، الإحصاء
`1 live (1 proven, 1 refused, 1 line)`.

---

## TASK R4 (D58) — سلم سمعة النظراء الدائم — مكتمل 2026-09-29

**ماذا:** ذاكرة ضد المسمّمين: سلم لكل نظير `مراقبة ← عدم استجواب ← عزل ←
حظر` يصعد على الأحداث المؤكدة المعدودة فقط (نوعا R2: تناقض الدورة، المفتاح
المزيّف)، لا يهبط ولا يعفو — الاسترداد بيد المشغّل بتحرير ملف السمعة
وإعادة التشغيل. المفاتيح: `PEER REPUTATION: ON` (OFF افتراضاً = stock حرفياً)،
`PEER REPUTATION FILE:` (افتراضي `multics.peers` بجانب الإعداد)،
`DISTRUST/ISOLATE/BAN:` 5/20/50. عدم الاستجواب يكفّ عن الطلب، العزل يرفض
عند الباب ويظل يعدّ، الحظر يسحب FLAG_DISABLE ويتجمد السجل. كتابة ذرية
(tmp+rename) عند كل تصعيد؛ إعادة التشغيل تعيد التسليح من الملف؛ سطر
`[PEER REP] ... ESCALATED` موقّع لكل تصعيد؛ `telnet PEERREP` للجدول.

**الأدلة:** `test_peerrep` 33/33؛ هدف `pr` الحي 14/14 بخمس أطوار (~110 ث:
الصعود، تثبيت الملف، بوابة السؤال، العزل والعدّ، الحظر والتجميد، إعادة
التسليح، الاسترداد)؛ `make -C tests all` exit 0 في تمريرة واحدة (~25 د)؛
soak 1200 ث PASS (1202 ث، 382 ECM، RSS 1892 kB). فخّان حيّان أُصلحا
على الطريق: قارئ العتبات كان يأخذ حرف الخيار بعد أن تمسحه القيمة
(العتبات بقت بالافتراضي بصمت — صار يُلتقط أولاً ويُسجَّل تطبيقه)، والتصعيد
كان يسجّل قبل تثبيت الملف (صار التثبيت أولاً).

**الإصدارات:** stock `7ef8001aec52a5edfded3867fe7e0d08`، stats
`20e89edd9447540c0a14300208e93554`؛ سلاسل `PEER REP` 8/8 في النكهتين،
بوابة رموز dcwstats 0/2 قائمة. اكتشاف stock موثّق بلا إصلاح (خارج النطاق):
الرد المرفوض أولًا يسمّم مدخل ECM ويمنع تسليم المفتاح الصادق اللاحق
(تداخل ردود stock تحت فيض R4) — مرشّح لجولة قادمة بقرار مستقل.

---

## TASK R5 (D59) — نقطة القراءة الآلية `GET /json` — مكتمل 2026-09-29

**ماذا:** كل أرقام أربع جولات كانت حبيسة HTML وتيلنت — R5 فتحها للآلات:
مسار واحد خلف نفس باب Basic (وقوائم/تأخير R3 قبله) يعيد مستنداً واحداً:
الإصدار والتشغيل، عدّادات dcwstats كلها مسمّاة، سلّم R4 بعتباته وسجلاته
(المرحلة رقماً واسمًا)، وصفوف النظراء مدمجةً فيها حالة السلم. باني نقي
`monjson.{c,h}` بلا مكتبات خارجية — المستند الذي يفيض يُهمل كله ولا
يُخدَم مقطوعاً. لا مفتاح إعداد جديد ولا منفذ جديد؛ نكهة stock تخدم
الشكل نفسه بـ`on=0`.

**الأدلة:** `test_monjson` 26/26 (أمسك خطأ فواصل حقيقياً قبل أن يُخدَم
بايت واحد: وسيط طول في `mjput` أكل فاصلات الأقسام وجعل المستند JSON
غير صالح)؛ هدف `jm` الحي 8 أطوار ~62 ث (401 الغريب، الأقسام الستة،
الأسماء كأعداد، العتبات المسلحة، صف النظير الصادق، فيض تسميم حقيقي
escalated ويُقرأ من المستند ويدمج في الصف)؛ `make -C tests all` exit 0
في تمريرة واحدة (429 ok)؛ soak 1200 ث PASS (1203 ث، 382 ECM، RSS 1964 kB).

**الإصدارات:** stock `394559fb634515ed42e3ac33ac6202f8`، stats
`f304622469748a478e5ce5cbe41cea1d`، dev `ffba2abba5c281507eaa0833d9cf0ced`.
مصفوفة الثقة لكل قناة بقيت داخلية عمداً (وسيط تنقيط لا سطح تنبيه) —
موثّق في D59.
