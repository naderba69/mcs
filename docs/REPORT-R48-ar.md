# تقرير R48 — تنفيذ جزئي، بوابات الإغلاق ما زالت مفتوحة

**حالة التسليم:** غير مكتمل. هذا التقرير لا يعلن اكتمال R48 ولا انتقالاً إلى R49.

- **مرجع القرار:** D111 (الملف الحاكم الذي قدّمه المشغّل).
- **الفرع:** `arena/cb2cb74d-mcs`.
- **التاريخ:** 2026-10-10 (تاريخ Arena الحالي).

## D111 — النطاق الحاكم وقيود السلامة

D111 الذي قدّمه المشغّل هو المرجع الحاكم. نصه الأصلي الكامل غير مضمّن في هذه
الشجرة؛ هذا الملخص لا يوسّعه ولا يستبدله، ويسجل فقط القيود الصريحة المحفوظة في
طلب العمل الحالي:

- البند الأول بوابة إلزامية لما بعده؛ لا يُعلن R48 أخضر أو مكتمل ما دامت بوابة
  مطلوبة أو مراجعة ضرورية مفتوحة.
- مطابقة D-1 الإيجابية تأخذ نوع مصدر المرشح ونوع مصدر المرجع صراحةً. في مسار
  R48 يجب أن يكون المرشح cache والمرجع line-server للـECM نفسه؛ المرشح غير
  cache لا يستهلك رصيد المطابقة ولا ينشئ حقيقة سمعة cache. hash أو pointer وحده
  ليس هوية ECM دقيقة عند إعادة استخدام pool slot؛ يُستخدم `ecmd5` أو lifetime
  الطلب في سياق الإرسال، ويظل الرد cache المؤجل في `cache_reply_buf`.
- `CACHE EVIDENCE` اختياري وOFF افتراضياً. لا تغيير لبروتوكول الأقران أو العميل،
  ولا واجهة عامة جديدة، ولا تعديل لسلوك الملفات المسلّمة أو تخمين لقيم CAID؛
  إعدادات الباقات تختلف بحسب CAID.
- لا تغيير للذاكرة السلبية أو attestation المحافظة بلا اختبار يعيد إنتاج العطل.
  D-4 جزئي؛ تنفيذ الدرجة الأولى لا يعني إغلاق بقية الدرجات.

## الخلاصة التنفيذية

اكتمل تنفيذ واختبار لبنة ترتيب المصدر الدليلي الأولى: سمعة اختيارية ومحدودة
`(source, CAID, provider)`، انتقاء بين المرشحين الموجودين عند البحث، والدرجة
الأولى فقط من D-4: خفض الترتيب إلى صفر لمدة 15 دقيقة بعد إثبات سلبي، ثم اشتراط
عينة مرجعية نظيفة ذات tick لاحق قبل استعادة الترتيب. التأخير ترتيب فقط، لا
يحذف المصدر ولا يستبعده؛ واختبار التكامل يثبت أن الاختيار يظل غير فارغ عندما
تكون كل المرشحات مؤخرة.

يلتقط ledger أهلية `CACHE EVIDENCE` عند أول قبول ناجح لكل مرشح: قبول أول
والخيار OFF لا يُرقّى بأثر رجعي إذا صار الخيار ON ثم قُبل المرشح نفسه مجدداً؛
اختبار ledger يغطي هذا الانتقال، وبلغت نتيجة إعادة الاختبار بعد قيد نوع المصدر
**119/119**.

هذا **ليس** تنفيذ D-4 كاملاً ولا يحقق أبواب قبول R48 كلها. لا توجد بيانات حركة
حقيقية أو قياس p95 ميداني، ولا تجربة CAID 1814؛ قيمة الباقة تُقرأ من الـpreset
وتتقدم عليها قيمة `CACHE TIMEOUT` الصريحة الموجودة أصلاً. لم تُغيَّر إعدادات
الباقات المسلّمة لتسليح `CACHE EVIDENCE`، وتبقى الميزة OFF افتراضياً.

## D112 — قرار التنفيذ المحلي وما بُني

- حقائق مصدر×كايد×مزود في جدول محدود (128 صفاً)، مع فصل المزوّد المعلوم عن
  المجهول، وEWMA ثابت النقطة يتجه إلى المحايد مرة في الدقيقة. لا تكسب الموافقة
  بين پييرين أي رصيد؛ helper دفتر المطابقة يستقبل نوع المرشح والمرجع صراحةً،
  والإنتاج يقيّدهما إلى cache وline server، لذلك لا يستهلك مرشح غير cache
  الرصيد ولا ينشئ حقيقة سمعة cache.
- ترتيب المرشحين المؤهلين وفق السمعة ثم منشأ الخط ثم كاسر تعادل مستقر. لا
  ينتظر النظام وصول مرشح مستقبلي. عند التعادل تُسلّح آلية التحقق الموجودة.
- أول درجة من D-4 فقط: 15 دقيقة من rank=0، وخروج يتطلب دليلاً مرجعياً نظيفاً
  بعد الإثبات ومرور المدة. أحداث ledger المتأخرة لا تُحسب عينة خروج، وإثبات
  جديد في الـtick نفسه يعيد تصفير عدّاد العينات النظيفة.
- عرض snapshot السمعة ووقت آخر دليل/إثبات وعدّاد العينات النظيفة وحالة التأخير
  عبر `/json` وصفحة `/cwlog` القائمتين؛ لا endpoint جديد.
- فحص cfgcheck يستمر في إثبات أن `CACHE EVIDENCE` غير مسلّح في الملفين
  المُسلّمين. تحذير «لا سجلات خطوط صادرة» في الملفين متوقع ومقصود: يعرّف
  بروفايلات الكاش فقط ولا يُخفى أو يُعامل كنجاح وجود مرجع.
- عولجت مسارات إرسال Newcamd النشطة محلياً: مساعد الرد يحرّر قفل حالة العميل
  قبل حيازة قفل I/O والكتابة/الانتظار في `poll`؛ ويُبطل الفصل جيل الجلسة ويحمي
  إغلاق الـsocket بقفل I/O منفصل. تحقق تسليم CW يقارن
  `cli->ecm.lifetime_id` بالـsnapshot تحت قفل العميل قبل الإرسال وبعده، ولا
  ينشر `STAT_DCW_SENT` أو يمسح busy إلا بعد نجاح النقل. مسار ACK الأولي يضيف
  `login_pending` لكل عميل، ويحرر قفل حالة العميل و`prg.lockcli` قبل TCP I/O؛
  ويبقى mutex العام محصوراً في البحث/حجز المحاولة القصير. اجتاز harness socket
  حقيقي بعد إعادة البناء: تشبع TCP إلى EAGAIN. في بناء poll سُجّلت تاريخياً
  استجابتان لصفحة `/newcamd` خلال **2.376ms** و**3.646ms** أثناء poll الإنتاج
  رقم **7**؛ وآخر إعادة poll سجلت **2.015ms** أثناء poll رقم **6** عند
  +**1,737µs**. في بناء الإنتاج EPOLL سُجّلت **1.843ms** أثناء poll رقم **6**
  عند +**5,434µs**. اجتاز السيناريو المضبوط في المسارين: رُفضت المحاولة
  المكررة فوراً بينما ACK محجوز، وانتهى poll الإنتاج بمهلة 100ms، ثم حصل تسجيل
  جديد على LOGIN_ACK قبل RST المؤخر؛ بقي الخادم حياً. هذا يثبت تداخل ACK
  المضبوط فقط، لا حلقة dispatch كاملة أو كل سباقات الفصل/fd-reuse/reload.
  ضُيّق pin الإعدادات قبل LOGIN: العامل ينسخ profile ID والجيل والمفتاح، ثم
  يحرر read pin حول إرسال مفتاح التهيئة واستقبال LOGIN؛ وبعدها يعيد pin ويتحقق
  من الجيل والملف والمفتاح قبل استعمال إعدادات الملف/العميل. اختبار
  `newcamd-config-reload` أوقف العميل بعد keymod وقبل LOGIN، وأثبت عبر صفحة `/`
  المصادق عليها أن عنوان الإعداد تغيّر قبل تحرير العميل خلال بوابة **2.5s**؛
  آخر نتيجة poll كانت **2.0ms**، ونتيجة EPOLL **2.8ms**. أُغلق LOGIN القديم بعد
  ذلك بلا جواب صالح، وبقي الخادم حياً. لا يغطي هذا كل تداخلات reload أو إعادة
  استخدام fd.
  يظل read pin ممسوكاً أثناء التحقق من بيانات الدخول وإرسال ACK ونشر الجلسة؛
  قفل حالة العميل و`prg.lockcli` محرران حول TCP I/O/poll، لكن قفل عمر الإعدادات
  ليس كذلك. `send_nonb()` يعيد الفشل عند poll بلا تقدم 100ms، لكنه لا يملك مهلة
  كلية عند تقدم جزئي أو EINTR متكرر؛ الاختبارات تثبت timeout بلا تقدم فقط.
  اختبارات `test_newcamd_lifetime` اصطناعية و`ncclient` فحص بروتوكولي، وليسا
  إثباتاً شاملاً. كما وجدت المراجعة قراءات runtime غير محمية في قوائم Newcamd
  على الويب؛ صارت القوائم تأخذ snapshot تحت قفل العميل وتحرره قبل الكتابة إلى
  HTTP socket. اجتاز `xs` على البناء المحدّث وفحص Newcamd العميل الحقيقي، لكن
  ذلك لا يثبت غياب سباق أو عيب آخر. هذا ليس تدقيقاً شاملاً لكل أقفال Newcamd أو
  بقية بروتوكولات العميل.
- مراجعة CCcam المحلية الجزئية: أُضيف I/O guard مستقل، وتسلسل المسارات المعدّلة
  يكون I/O ثم قفل حالة قصير. مساعد الإرسال يحرر قفل الحالة قبل
  `cc_msg_send()`/`cc_msg_send_and_encrypt()` ثم يعيده؛ ومسار الاستقبال يحرره
  قبل `cc_peekmsg()` أو `cc_msg_recv()` (قد ينتظر الأخير حتى 3 ثوانٍ في بناء
  `RECVMSG_PEEK`). يبقى I/O guard ممسوكاً أثناء socket I/O لتثبيت الـdescriptor؛
  لا يُمسك `ecm_lock` أثناء رد ECM الفاشل الموجود مسبقاً أو رد فشل التخصيص، كما
  أن مسارات تسليم CW تلتقط snapshot ثم تحرره قبل الإرسال. جُمّد نشر حقول
  `lastecm`/حالة الإرسال إلى ما بعد نجاح الكتابة. هذه مراجعة للمسارات المعدّلة،
  لا شهادة لكل كتابات CCcam.

  أُضيف جيل جلسة 32-bit غير صفري عند الإرفاق النهائي. لقطة poll تحفظ المقبس
  والجيل معاً؛ والتحقق عند dispatch يعيد المطابقة تحت I/O/state قبل القراءة أو
  الفصل. رمز EPOLL_CCCAM الاختياري يحمل معرّف العميل والجيل. عامل CacheEX mode 3
  يتلقى المقبس والجيل من عامل القبول، يحرر قفل الحالة قبل `poll()` ثم يعيد
  التحقق قبل receive/disconnect؛ يثبت I/O guard الـsession أثناء فحص نهائي
  ونشر `tid` عند إنشاء العامل. في CacheEX mode 2 أُزيل `prg.locksrv` عن مسارات
  عملاء CCcam أثناء dispatch، مع إبقائه حول مسار الخوادم الذي يحتاجه. لا يُحتفظ
  بقفل حالة العميل أثناء `poll()` أو socket I/O؛ قد يبقى I/O guard ممسوكاً أثناء
  قراءة/كتابة socket لحماية عمر المقبس. أُربط ACK وserver-info وبطاقات البداية
  بمقبس/جيل الإرفاق، وتُفحص `cardsent` تحت قفل الحالة. صُحح شرط `sendversion`
  ليقارن أحرف المصافحة بدل إسنادها إلى السلسلة الحية. كان مسار `cc_peekmsg()`
  الافتراضي يستعمل `nonblock_recv()` الذي يفقد القراءة الجزئية بين readiness
  events؛ أزيل helper غير القابل للتجميع الجزئي وأصبح `cc_peekmsg()` يحتفظ
  بالإطار المشفر الجزئي ويقدّم cipher state بعد اكتمال كل جزء. اختبار socketpair
  يجزّئ header وbody ويثبت حفظ البايتات وحالة cipher (**6/6**). يغطي فحص الأقفال
  المسارات المعدلة فقط: ترتيبها I/O ثم state القصير، و`ecm_lock` محرر قبل ردود
  ECM. لا يكتمل بذلك تدقيق جميع الأقفال/البروتوكولات؛ config read pin يبقى أثناء
  dispatch، وخيار `RECVMSG_PEEK` قد يمسك I/O guard خلال `cc_msg_recv()` حتى 3s
  بلا قفل حالة. أضيف اختبار helper يغطي رفض generation قديم مع إعادة استخدام
  رقم fd، وترميز epoll، وتخطي الصفر عند الالتفاف، وتفرّد تخصيص الأجيال
  المتزامن عبر الالتفاف.

  بناء x64 الإنتاجي يختبر مسار CCcam poll لا EPOLL_CCCAM؛ وفحص `-fsyntax-only`
  مع `EPOLL_CCCAM` اجتاز، وبناء مؤقت خارج المستودع مع `-DEPOLL_CCCAM` اجتاز
  `cccam-trace-success.py` في وضع `full`، لكن لا يوجد اختبار تكاملي يفرض
  HUP/قراءة قديمة بعد reconnect حي. اختبارات trace/recycle وhelper تختبر
  مكونات محددة ولا تثبت السباق الحي. الجيل 32-bit قد يعاد استخدامه بعد دورة
  كاملة. كما أن عمر الكائن مستقل عن جيل الجلسة: عاملا المصافحة الواردة
  `cc_connect_cli` و`freecc_connect_cli` يمسكان الآن pin قراءة الإعدادات
  طوال المصافحة عبر غلاف، فلا يستطيع reload استعادة server/config تحتهما؛
  محاسب `server_connect_wait_idle()` يغطي عمال الاتصال الصادر. لم تُقَس مدة
  تأخير reload الناتجة عن pin المصافحة. تظل حلقة dispatch ممسكة بقفل قراءة الإعدادات أثناء معالجة
  الحدث، وقد يمتد ذلك إلى انتظار socket؛ لم تُقَس مدة تأخير reload. كتابات
  FreeCCcam المباشرة نُقلت تحت I/O guard في هذه اللبنة (frcc_disconnect_cli
  وfrcc_senddcw_cli وECM_NOK/KEEPALIVE/CLI_INFO) مع تحرير ecm_lock قبل الإرسال
  في مسار store_ecmdata الفاشل؛ لم يُجر اختبار تنازع/backpressure مستقل بعد.
  مسارا رد ECM الفاشل مسبقاً وفشل التخصيص
  راجعا مصدرّياً وبُنِيا، لكن لا يوجد اختبار يفرض backpressure عليهما أو يثبت
  ديناميكياً تحرير `ecm_lock` وقت الإرسال. شُغّل ASan/UBSan على تتبع CCcam
  في أوضاع `full` و`half` و`non09half` ونجح، لكن لم يُجر اختبار تنازع/backpressure
  مستقل، ولا يثبت sanitizer سلامة الأقفال. هذه الديون تمنع إعلان اكتمال تدقيق CCcam.

## التحقق المسجل

| الفحص | النتيجة |
|---|---|
| `make -C make-x64 test` | **PASS، exit 0** بعد تغييرات generation ومحلل CCcam المجزأ. `test_ledger` = **119/119**، بما فيها بوابة D-1؛ `test_cccam_session` يغطي الجيل غير الصفري/الالتفاف، التفرد المتزامن، رفض حدث قديم على fd معاد الاستخدام، وترميز EPOLL؛ و`test_cccam_peekmsg` اجتاز **6/6** لفصل header/body وحالة cipher. اجتازت اختبارات `test_newcamd_lifetime` الاصطناعية؛ ظهرت تحذيرات تجميع غير قاتلة في helpers واختبارات قديمة. |
| `make -C src target=x64 link -B -j2` | **PASS، exit 0** بعد تغييرات CCcam؛ بناء x64 يتضمن EPOLL لـNewcamd/ECM، بينما CCcam في هذا البناء على مسار poll. لا يستهدف `srv-cccam.c` منفرداً لأنه مضمن في `main.c`. دوال `*_hitprofile` في `setdcw.c` صارت `static inline` ليربط بناء ASan/UBSan دون ترقيع مؤقت. |
| `gcc -fsyntax-only ... -DEPOLL_CCCAM ../src/main.c` داخل `src/` مع defines الإنتاج | **PASS، exit 0**؛ تحقق ترجمة الفرع الاختياري EPOLL_CCCAM. بناء مؤقت خارج المستودع مع `-DEPOLL_CCCAM` اجتاز `cccam-trace-success.py` في وضع `full`؛ لا يثبت reconnect حي أو سباق حدث قديم. |
| `python3 tests/cccam-trace-success.py src/x64/multics` | **PASS**، `mode=full`؛ تتبع ECM اصطناعي متكامل، وليس اختبار تنازع أقفال أو سباق generation حي. |
| بناء مؤقت ASan/UBSan خارج المستودع + `cccam-trace-success.py` | **PASS** في أوضاع `full` و`half` و`non09half` مع `detect_leaks=0`؛ لا يثبت سلامة الأقفال أو سباقات الجلسات. |
| `python3 tests/cccam-setdcw-recycle.py` | **PASS** لحالتي `before-snapshot` و`before-commit`؛ في كلتيهما reused-slot success/old CW = `0 0`. يختبر منع تطبيق CW قديم على خانة ECM معاد استخدامها، لا تزامن socket. |
| `python3 tests/cccam-setdcw-trace-identity.py` | **PASS** لحالتي هوية trace `zero` و`different`؛ لا يثبت سباقات الجلسة أو أقفال I/O. |
| `make -C src target=x64 link -j2` | **PASS، exit 0** في تحقق R48 الأسبق، مع smoke واجهة الويب للبناء الناتج. |
| `make -C tests newcamd-ack-race` على بناء `make-x64` ذي poll | **PASS، exit 0**: TCP send queue تشبع إلى EAGAIN؛ أحدث قياس poll للصفحة `/newcamd` **2.015ms** أثناء poll إنتاج رقم 6 (+1,737µs). القياسان 2.376ms و3.646ms أثناء poll رقم 7 تاريخيان. duplicate login رُفض أثناء تعليق ACK، وانتهى poll بمهلة 100ms، ثم حصل تسجيل جديد على LOGIN_ACK قبل RST المؤخر؛ بقي الخادم حياً. لا يثبت كل حالات dispatch/fd-reuse/reload أو تقدم TCP الجزئي. |
| `make -C tests newcamd-config-reload` على بناء `make-x64` ذي poll | **PASS، exit 0**: العميل أُوقف بعد keymod وقبل LOGIN؛ تغيّر العنوان في الصفحة المصادق عليها `/` خلال بوابة 2.5s، والقياس **2.0ms**؛ LOGIN من الجيل السابق رُفض وأُغلق بلا جواب صالح، والخادم بقي حياً. يغطي هذا مسار الانتظار قبل LOGIN فقط. |
| `make -C tests smoke ncclient newcamd-config-reload newcamd-ack-race BIN=../src/x64/multics` على EPOLL | **PASS، exit 0**: smoke وعميل Newcamd الحقيقي (مصافحة وطلبا ECM؛ decode-failed متوقع بلا card server) وreload وACK race. ACK: `/newcamd` خلال **1.843ms** أثناء poll رقم 6 (+5,434µs) مع EAGAIN وtimeout 100ms؛ reload: **2.8ms** ضمن بوابة 2.5s؛ رُفض LOGIN القديم وبقي الخادم حياً. اختبارات سيناريوهات محددة، وليست إثباتاً شاملاً لكل سباقات الجلسات/dispatch. |
| أهداف `make -C make-x64`: `release-queue`, `release-queue-stats`, `release`, `release-stats` | **PASS، exit 0** لكل هدف بعد قيد نوع المصدر؛ `bin/multics.x64` يطابق `bin/multics-r82a-stats-x64` بتاً. |
| `make -C tests smoke` على stock وqueue، و`r48-web` على stats وqueue-stats، وsmoke إضافي لـ`src/x64/multics` | **PASS** للبناءات الخمسة: `/`, `/threads`, `/json`؛ و`/cwlog` على stats وqueue-stats. هذا smoke محلي ولا يثبت حركة عميل ميدانية. |
| `make -C tests all` | **غير مكتمل؛ لا يُحسب PASS**. انتهت مهلة الأداة بعد 1800 ثانية أثناء بدء الهدف `fk`، ولا يوجد exit code ناجح للمجموعة. لا تُستخدم هذه المحاولة لإثبات سلامة الجناح كاملاً. |
| `make -C tests fk` منفرداً | **PASS، exit 0**، `fk: 3/3 ok` بعد انتهاء مهلة `all`. |
| الأهداف اللاحقة المنفصلة `fz at md sc qv cf rt ecm-radegast-identity cacheex-provider-collision cache-check-characterization cache-cross-provider cache-cross-gates` | **PASS، exit 0** في تشغيل منفصل. النتائج المعلنة: `fz 2/2`، `at 3/3`، `md 3/3`، `sc 9/9`، `qv 4/4`؛ ونجحت اختبارات R41/R43 وRadegast identity وCacheEX provider collision وcache-check-characterization وcache-cross-provider وcache-cross-gates. لا يحوّل نجاح المجموعات المنفصلة تشغيل `all` غير المكتمل إلى PASS شامل. |
| فحص مزامنة FZ2 من واجهة الويب ضمن الهدف المنفصل `fz` | **PASS**: `tests/peerrep-web.py` يقرأ `/json` و`/cache`؛ أثبت `Sent REQ` من 0 إلى 1 مع `received_reply=0` و`hits=0` وبقاء العميل نشطاً، ثم أكد ECM pending قبل وصول الرد. إذا لم تُظهر الواجهة pending يفشل الاختبار. لم تُستخدم سجلات الخادم/العميل أو ملفات `.ce-*` لإثبات التزامن، ولم تُغيّر إعدادات الويب. هذا تحقق تكاملي، لا تشخيص تشغيل ميداني. |
| تدقيق fallback timeout الخاص بجدول Provider | **معطّل (مراجعة R48، محدّثة)**: وُجد أن helpers المزوّد موصولة فعليًا في الإنتاج (`th-ecm.c` للمهلة/زمن الكاش، `setdcw.c`/`cli-cccam.c` لـCRC والدورة). مراجعة ترتيب الاستدعاء أثبتت عدم وجود مسار segfault: `provider_cfg_tab` عمومي مُصفَّر، و`count=0` آمن، وكل helpers آمنة على NULL. تجربة تفعيل (`provider_config_init()` ON مع CAID 1884 في الجدول) نجحت في `cache-consensus-three-peers` 4/4 وأوضاع `cccam-trace-success`، فملاحظة «causes segfault» القديمة غير صحيحة للكود الحالي. العائق الحقيقي: تغيير سلوك غير مُتحقق ميدانيًا (مهلات أقصر 350–1500ms، تعطيل CRC، بوابات دورة لكل مزود) لـ100 مدخل من أبحاث منتديات. يبقى `provider_config_init()` معطّلًا في `src/main.c` (الجدول فارغ = stock defaults) حتى يوجد تحقق ميداني لكل مزود. `test_provider_config` يختبر التهيئة مباشرة (14/14). |
| `make -C tests cfgcheck` بعد تصحيح نطاقات المهلة | **PASS، exit 0** للملفين واختبارات كشف العمى ونطاقات parser. `CACHE TIMEOUT` = 0..5000ms و`SERVER TIMEOUT` = 300..10000ms؛ تحذير غياب مراسي الخطوط الصادرة متوقع لبروفايلات الكاش فقط، ولا يُحسب نجاحاً لوجود مرجع. |
| `make -C tests docrefs` بعد تحديث التقرير والقرار | **PASS، exit 0**: 344 مرجعاً، 0 خارج النطاق؛ 118 تنبيهاً للمراجعة المحتوائية advisory، لا تمنع الخروج 0. |
| `git diff --check` | **PASS** على الشجرة النهائية الموثقة أدناه. |
| `md5sum -c MANIFEST-md5` | **PASS، 810/810** بعد إعادة توليده على الملفات المتتبعة وملفات R48 المقصودة، ومنها اختبارات CCcam المجزأ والجلسة؛ المانيفست يستبعد نفسه وآثار البناء غير المتعقبة. |
| `tests/soak.sh`, `SOAK_SECONDS=421` بعد تعديل Newcamd | **PASS تاريخي، لم يُعَد بعد latch الـopt-in**: 426 ثانية، 133 ECM، لا إخفاق اتصال، عدادات Telnet/الويب متطابقة (المجموع 1,391). RSS عند العينة الأساسية 5,740 KB، الأقصى/النهائي 5,740 KB (نمو 0)، دون السقفين. لا يُدّعى soak جديد على الشجرة النهائية. |

## SuperSmartFilter v6.0 — الكونسول والإعدادات (2026-11-13)

نُفّذت مواصفة v6.0 كاملة على فرع `arena/59c38810-mcs` (commits `518dfa9`، `6cf4eb0`، `139eba1`):

- **مستويات الفلتر لكل بروفايل** (`518dfa9`): `FILTER: <0-20>` لكل بروفايل؛ المستوى يحدد نافذة الرفض الفعالة (eft) وعمق التاريخ (bd): L2=100%/1، L3=75%/2، L4=60%/2، L5=50%/3، L6=100%/3 (بوابة HALFNULLED، skipcwc لا يتجاوز). Nagra (18xx) وNDS (09xx): `FILTER: 6` (36 بروفايل)؛ الباقي: `FILTER: 3` (36 بروفايل).
- **كونسل SmartFilter** (`6cf4eb0`): صفحة `/smartfilter` بأقسام Overview (عدادات + تفصيل أسباب الرفض + ملخص الإعدادات)، Recent CW (حلقة السجل)، Peers (فلاتر All/Strongest/Bad عبر `?filter=`)، Profiles (مستوى FILTER لكل بروفايل)، Logs (رابط عارض السجلات). لقطات قراءة فقط تحت `ssf_lock` (لا تغيير حالة). (صفحة `/donate` أُضيفت ثم حُذفت بطلب المشغّل.)
- **إعدادات SF_* الكاملة** (`139eba1`): تحليل `CACHE SF LEVEL/TIME/BADCWRATIO/BADCWRATIOMIN/MINREPLIES/UNBLOCK/DROPBAD/BLOCKFAKE/ENTROPY/PATTERN/DUPSEEN/HISTORY/CWFMT AUTO/CWFMT ENFORCE/MC MATCH/MC MODE/MC WAIT` مع تقييد المدى؛ الافتراضيات تطابق v6.0. الحظر العادل: نسبة المفاتيح السيئة فقط (≥85% بعد ≥300 CW و≥10 ردود، فك تلقائي بعد 600s) — المنطق في `ssf_record_cw_result()`.

التحقق المسجل لهذه اللبنة: بناء `make -C src target=x64 link -B -j2` نظيف؛ `make -C make-x64 test` كل الحزم `0 failed`؛ `cache-consensus-three-peers` PASS؛ إقلاع حي قرأ FILTER لكل بروفايل (36×6 و36×3 في صفحة `/smartfilter`)؛ `make -C tests smoke` يفحص `/smartfilter` (الأقسام الخمسة) وفلاتر Strongest/Bad و`/donate`؛ `MANIFEST-md5` 822/822. لا يُدّعى تحقق ميداني لأي كشف فايك؛ `DCWFILTER PATTERN: OFF` في بروفايل 1814 مقصود.

### بصمات النكهات المبنية

| الملف | MD5 |
|---|---|
| `bin/multics-r82a-stock-x64` | `03d3068ada2d92750a920a2375641b2c` |
| `bin/multics-r82a-stats-x64` | `5ad9574e5b1d872939c2a48798cea590` |
| `bin/multics-r82a-queue-x64` | `3932025dd54ebd75336c2b664c48c0c4` |
| `bin/multics-r82a-queue-stats-x64` | `8b594513f7bbfb3cfb13d96e5b4c7c07` |
| `bin/multics.x64` (اسم التسليم الثابت؛ stats) | `5ad9574e5b1d872939c2a48798cea590` |

هذه بصمات بعد البناء الحالي. جُدّد `MANIFEST-md5` على 810 مسارات: 810 ملفات متتبعة عدا المانيفست نفسه؛ وتشمل اختبارات
`make-x64/test_cccam_peekmsg.c` و`make-x64/test_cccam_session.c` وملفات Newcamd المستهدفة (`tests/newcamd-ack-race.sh`،
`tests/newcamd-config-reload.sh`، `tests/newcamd-sendfault.c`). تحقّق
`md5sum -c` من الجميع؛ لا يشمل المانيفست نفسه ولا آثار البناء غير المقصودة.

## جدول الكايدات — جرد حقائق الملف والـpreset، لا توصيات تشغيل جديدة

الجدول التالي جرد آلي لملف `multics-r24-optimized.cfg` و`src/caid-optimizations.h`.
72 بروفايل تتوزع على 53 كايداً فريداً. العمود «النافذة الفعالة» يطبّق ترتيب
الأولوية القائم: `CACHE TIMEOUT` الصريح، ثم `SERVER TIMEOUT` الصريح، ثم
الـpreset. حقول الحماية أدناه هي قيم الملف الحالي، وليست توصية بتغييرها أو
بتسليح `CACHE EVIDENCE`. إعداد كل باقة يبقى مستقلاً كما طلب المشغّل.

| CAID | البروفايل/ات | النوع | preset (ms) | النافذة المهيّأة الفعالة (ms) | الحماية القائمة في optimized |
|---:|---|---|---:|---:|---|
| 0100 | NC+ CYFRA - 0100/68، Eleven - 0100/6A+6C، Movistar+ - 004106، MEO Portugal - 0100/5221، Telesat - 00006D، MEO Portugal - 5221/5211/5225 | SECA (fallback) | 250 | 800, 1000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0500 | FRA - 32830، BIS TV - 042820، SRG Swiss + Viaccess - 0500، TNTSAT - 030B00، Vision Ukraine - 0500، SRG/SRF - 0500/508، Love Natural 4K - 0500، Hayat - 0500، HRT - 0500/16E، SCT + Penthouse HD - 0500، France Sat - 0500، Redlight - 0500 | Viaccess | 250 | 3000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0604 | ART - 0604 | Irdeto | 250 | 600 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0606 | Ziggo - 0606، Albanicasat - 0606 | Irdeto | 250 | 600 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0609 | Orange SK - 0609 | Irdeto | 250 | 600 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0629 | Pink - 0629 | Irdeto | 250 | 600 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 090D | YES+AMOS - 090D | VideoGuard | 250 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 090F | Viasat - 090F | VideoGuard | 250 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 091F | TOTAL TV - 091F | VideoGuard | 300 | 300 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 092B | D-SMART - 092B | VideoGuard | 250 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 092F | DolceTV - 092F | VideoGuard | 220 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 0930 | YES 4W - 0930 | VideoGuard | 250 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 093E | Viasat - 093E | VideoGuard | 250 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 0960 | Sky UK - 0960 | VideoGuard | 250 | 450 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 0963 | SkyUK - 0963 | VideoGuard | 250 | 450 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 098D | Sky DE - 098D | VideoGuard | 250 | 450 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 098E | Kabel NDS - 098E | VideoGuard | 250 | 450 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 09BD | Vivacom - 09BD | VideoGuard | 280 | 280 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 09BE | COSMOTE - 09BE | VideoGuard | 280 | 350 | check=YES، half=YES، pattern=OFF، evidence=unset |
| 0B00 | DigitalALB - 0B00 | Conax | 200 | 5000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0B01 | SAT BG - 0B01 | Conax | 200 | 4000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0B02 | Focus - 0B02 | Conax | 200 | 3000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0B0F | XTRA TV - 0B0F | Conax | 200 | 3000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0BAA | Tring Digital - 0BAA | Conax | 200 | 3000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0D95 | AUSTRIASAT - 0D95 | CryptoWorks | 230 | 700 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0D96 | Skylink - 0D96 | CryptoWorks | 230 | 700 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0D97 | 0D97 - 0600 | CryptoWorks | 230 | 700 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 0D98 | ORF - 0D98 | CryptoWorks | 230 | 700 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1010 | Profile_Tandberg | Tandberg | 320 | 250 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1708 | OSN - 1708 | BetaCrypt (fallback) | 250 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1802 | NOS SGPS - 1802، DIGI TV - 1802 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1803 | POLSAT - 1803/1861 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1805 | Mediaset DTT - 1805 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1810 | MovieStar - 1810 | Nagra | 300 | 300 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1811 | CSAT NG - 1811 | Nagra | 320 | 320 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1813 | NC+ - 1813 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1814 | MEO PT - 1814 | Nagra | 280 | **400** | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1815 | UPC - 1815 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1817 | Canal Digital 23.5E - 1817، C+NL Nagra - 1817 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1818 | Canal Digital 23.5E - 1818 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1819 | AB SAT - 1819 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1830 | HD+ 1830 | Nagra | 300 | 1000 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 183D | TivuSat - 183D | Nagra | 280 | 350 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 183E | TVSat - 183E | Nagra | 280 | 350 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1840 | Mediaset DTT - 1840 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1841 | Virgin Cable UK - 1841 | Nagra | 280 | 250 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1843 | HD+ - 1843 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1860 | HD+ 4K - 1860 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1880 | RCS/DIGI TV - 1880 | Nagra | 280 | 400 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1883 | C+FR - 1883 | Nagra | 350 | 350 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 1884 | NC+ 4K - 1884 | Nagra | 500 | 500 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 2600 | Profile_BISS | BISS (fallback) | 200 | 250 | check=YES، half=NO، pattern=OFF، evidence=unset |
| 4AEE | Bulsatcom - 4AEE | DREcrypt (fallback) | 200 | 350 | check=YES، half=NO، pattern=OFF، evidence=unset |

**مهم:** هذا الجدول جرد فعلي فقط، وليس توصية تشغيل جديدة. قيم البروفايلات
الصريحة، ومنها 1814 = 400ms مقابل preset=280ms، لم تُغيّر. `CACHE EVIDENCE`
غير مهيّأ/‏OFF في الملف المُسلَّم. قرار توصيات حماية مستقلة لكل كايد ما زال
ديناً مفتوحاً، ولا تُستعمل أسماء الخدمات دليلاً على بث حيّ أو إعداد صالح ميدانياً.

## أبواب R48 التي لم تُغلق بعد

1. **D-4 بعد الدرجة الأولى:** لا عزل قناة 60د بعد خلافين/10د، ولا عزل مزوّد
   عند إعادة استخدام CW أو >2% على 100 عينة، ولا فترات ثقة/شدّة موزونة، ولا
   مصنّف دوران مفاتيح مع تجميد العزلات وإعادة التعلم، ولا سقف 30% بمقام ثابت
   قبل الحدث. لا يجوز وصف هذا الباب بالأخضر.
2. **D-3 / الباقة العمياء:** لا محرك ميزانية 10%→5% وطبقات 1/3→1/20→1/50، ولا
   دفعات تدقيق عشوائية ونبض إبقاء الخطوط دافئة واستبعاد البردات من p50. لم يُبنَ
   الإنقاذ نحو العنقود البديل ولا كشف العمى التشغيلي الكامل ولا نشر مرساة المزوّد.
3. **الأحكام والمرساة:** لا يوجد فصل كامل للمتأخر عن الخاطئ في مسار الحكم، ولا
   إنذار مرساة كاذبة مع عمياء مؤقتة، ولا اختبار انتحال UDP يستعيد المصدر خلال
   60 دقيقة. واجهة السمعة ليست لوحة الحقيقة الكاملة المطلوبة.
4. **الزمن والإقلاع:** لم تُستكمل مراجعة الساعة الرتيبة لكل TTL/EWMA/نافذة، ولا
   الإقلاع البارد داخل ميزانية العينة. لا توجد استمرارية سمعة بعد restart.
5. **حقن الأعطال والقياسات:** لم تثبت أبواب 3 صادقين/47 منسوخين، التسميم ≤2 ECM،
   صفر عزل ظالم أثناء دوران المفاتيح، السم البطيء، البلاك الصامت محدود الضرر،
   والمرساة الكاذبة في لوحة الحقيقة. لا p95 ميداني ولا أسبوع قياس.
6. **التدقيق:** تدقيق مستهلكات دفتر ledger وسقف رصيد الپييرين من مصعد واحد ما زال
   مفتوحاً. عولجت مسارات إرسال/استقبال CCcam المحددة، لكن مراجعة جيل الجلسة
   لأحداث poll/epoll وCacheEX mode 3 ومدد pin الإعدادات لم تكتمل؛ وكتابات
   الكتابات المباشرة في FreeCCcam نُقلت تحت I/O guard في هذه اللبنة؛ لا يوجد اختبار مستقل لتنازع أقفال أو
   backpressure في CCcam بعد؛ شُغّل ASan/UBSan على تتبع CCcam فقط ولا يثبت سلامة الأقفال.
7. **التسليم والمراجعة:** `make -C make-x64 test` اجتاز (ledger 119/119) مع تحذيرات ترجمة غير قاتلة، لكن أحدث `make -C tests all` انتهى بمهلة 1800 ثانية عند بدء `fk` ولا يوجد نجاح شامل مثبت. اجتاز `fk` منفرداً 3/3، واجتازت الأهداف اللاحقة المسجلة تشغيلًا منفصلًا؛ لا يُجمع ذلك كنجاح `all`. بناء x64 واختبارات CCcam trace/recycle/identity المحددة واختبارات Newcamd المستهدفة اجتازت، كما سُجلت smokes محلية للنكهات الأربع. `docrefs` اجتاز 344 إحالة بلا مراجع خارج النطاق مع 118 تنبيهاً advisory؛ و`cfgcheck` اجتاز اختبارات الملفين ونطاقات المهلة وكشف البروفايلات العمياء، مع تحذير مراسي الخطوط المتوقع. `MANIFEST-md5` يشمل 810 مسارات، و`md5sum -c` تحقق من 810/810 و`git diff --check` اجتازا بعد التحديث الأخير. soak الـ421 ثانية تاريخي ولم يُعَد بعد latch الـopt-in. مراجعة line-by-line لكل الملفات المتغيرة والجديدة ما زالت مفتوحة، لذلك لا إعلان إنتاج أو اكتمال، ولا commit/push/PR update.
8. **مهلة Provider fallback:** أُعيد تعطيل التهيئة بعد مراجعة R48 (helpers موصولة فعليًا في `th-ecm.c`/`setdcw.c`/`cli-cccam.c`، فالملء كان سيغيّر المهلات وCRC والدورة دون تحقق). مراجعة ترتيب الاستدعاء (2026-11-12) أثبتت عدم وجود segfault وتجربة التفعيل نجحت تكامليًا؛ العائق المتبقي هو التحقق الميداني لكل مزود، لا عطل كود. الجدول فارغ في الإنتاج (= stock).

## استراتيجية الدورة (user-trace) — تنفيذ opt-in

نُفّذت الاستراتيجية المقترحة من تحليل `1814:005211` كخيارات opt-in (افتراضي OFF،
سلوك stock بايت-بايت عند الإطفاء):

- `CACHE CYCLE ERROR RESET: ON` — علامة `DCW_ERROR` على عقدة CW تُنطاق بالدورة:
  وصول بدورة معلنة مختلفة يمسح العلامة ويعيد التقييم بدل البلع الصامت.
  الأخطاء المحتوية (half-null/checksum) تحتفظ بعلامتها.
- `CACHE CYCLE ESTIMATE: ON` — عند غياب العلامة وفشل الفلتر، تُقدَّر الدورة زمنيًا
  من نمط نصف prevcw (نصف واحد مشترك = النصف الآخر دار) ثم يخضع التقدير لبوابة
  الدورة نفسها. **توسيع (2026-11-12)**: حالة "أول CW" (لا prevcw ولا دورة متوقعة)
  تُقدَّر من `ecmtag` (0x80→CW1، 0x81→CW0) — يفترض `cw1cycle==0x80` (الافتراضي
  الشائع)؛ إذا كان المزوّد يستخدم 0x81 فالتقدير معكوس وبوابة الدورة سترفض المفتاح
  (آمن). هذا يعالج حجب الكاش في التراس الحي (`1814:005211:13AE`) حيث consensus
  وصل إلى proceed لكن الفلتر حجب CW لغياب معلومات الدورة.
- `CACHE CONSENSUS INDEPENDENCE: ON` — ناخبو consensus من نفس host IP تُحتسب
  مصدرًا مستقلًا واحدًا (الاستقلال قبل الوزن)؛ OFF = عدّ peer IDs المميزة.
- `CACHE QUARANTINE: ON` — D-4 الدرجة الثانية: خلافان موزونان خلال 10 دقائق
  يعزلان القناة 60 ثانية، وخلافان عبر قنوات مزوّد واحد يعزلان المزوّد 60 ثانية
  (HOLD لمصدر الكاش فقط؛ خوادم البطاقات لا تُمس). OFF = consensus stock فقط.
- `CACHE STORM GUARD: ON` — حارس العاصفة: أكثر من 20 مفتاحًا/ثانية على قناة
  واحدة يحجز الفائض (HOLD لمصدر الكاش فقط). OFF = بلا تحديد معدل.

كلها لا تغيّر البروتوكول ولا الواجهات، ولا تُفعَّل في الملفات المُسلَّمة.
التحقق: `make -C src target=x64 link -B -j2` PASS، `make -C make-x64 test` PASS،
`cccam-trace-success.py` PASS mode=full، `cccam-setdcw-recycle.py` PASS،
`cfgcheck` PASS. لا يُدّعى إثبات سباق حي أو سلامة أقفال من هذه الاختبارات.

## طريقة التراجع

لإيقاف السلوك الجديد من دون تغيير البروتوكول أو استرجاع كود: اترك
`CACHE EVIDENCE` مطفأً (وهو كذلك في الملفات المُسلَّمة). التراجع المصدرّي عن
هذه اللبنة يجب أن يعكس كوحدة واحدة `src/r48-source-reputation.h`، وصلات
`ledger_act`/مرجع ledger، منتقي `cache_find_select_r48`، حقول snapshot في
`src/monjson.[ch]` و`src/httpserver.c`، واختباراتها؛ لا تُعكس تغييرات عمر ECM أو
طابور الكاش أو التغييرات المحلية السابقة مع هذه الوحدة من دون مراجعة وعزل.

## جولة الإنتاج 2026-11-14 — إصلاحات مُتحقق منها وديون مُسمّاة

Commits على `arena/59c38810-mcs`: ‏`033efd7`، ‏`172e455`، ‏`f9d5fe2`، ‏`b425bb7`، ‏`48a0a85`.

### أُصلح وتُحقق منه
- **تسرّب خانات ECM DoubleCheck** (`src/ecm-doublecheck.h`): الجدول (256 خانة)
  كان يمتلئ مرة واحدة ثم يفشل كل `ecm_doublecheck_start` بصمت؛ صارت الخانات
  المنتهية (>2× المهلة) تُستصلح عند البدء، والطوابع انتقلت من
  `(uint32_t)time(NULL)*1000` إلى `GetTickCount()` الرتيبة. اختبار جديد
  `make-x64/test_ecm_doublecheck.c` (11 فحصًا).
- **ساعات جدارية → رتيبة**: فاصل الضبط التلقائي في `main.c` وقاعدتا uptime في
  `diagnostics.c` و`web-dashboard.c` صارت على `getseconds()`.
- **تشخيص المستخدم المكرر** (`src/config.c`): أسطر `F:` و`MG:` المكررة تُحذَّر
  عند التحليل (كانت تُظلَّل بصمت لأن الدخول يُرجع أول تطابق). رصد فقط.
- **تغذية صفحتين حيّتين**: `/server-stats` و`/auto-tuning` كانتا تُعرضان أصفارًا
  دائمة؛ وُصّلتا بخطافات CCcam الحالية (عرض/اقتراح فقط، لا قرار فكّ أو توجيه).

### ديون مُسمّاة (مفتوحة، بلا إخفاء)
1. **`/cache-analytics` و`/diagnostics` بلا تغذية**: عدّاداتهما تبقى صفرًا.
   نقطة التغذية الصحيحة الوحيدة مركزية في `ecm_setdcw_origin` تحت `lockecm`
   في مسار no-thread؛ إضافة قفل قياس هناك تحتاج تدقيق ترتيب أقفال مستقلًا —
   لم تُفرض.
2. **ECM DoubleCheck لا يتحقق فعليًا**: لا مستدعٍ لـ`add_result/is_verified`؛
   البدء يُسجَّل فقط. التفعيل الكامل قرار بروتوكولي يحتاج تحققًا ميدانيًا.
3. **`cachewarm`/`predictive_warming`**: `cachewarm_tick` بلا مستدعين،
   و`predictive_warming_get_popular` يُستدعى في الحلقة الرئيسية للسجل فقط؛
   إرسال ECM إحماء فعلي تغيير بروتوكولي لم يُفرض.
4. **`advanced-optimizations` (parallel ECM)**: `parallel_ecm_init` فقط يُستدعى؛
   بقية الجدول ميت، وحقله `start_time` يُكتب ولا يُقرأ.
5. **`web_dashboard_init/start` معلّقان** في `main.c:3190` — لوحة الويب البديلة
   غير مفعّلة عمدًا.
6. البوابات الميدانية المعتادة: D-3/D-4 (دوران/باقة عمياء)، حقن الأعطال،
   وProvider fallback لكل مزوّد — تحتاج نظراء/بطاقات حقيقية.

التحقق لهذه الجولة: بناء نظيف، `make -C make-x64 test` ‏1153/0،
`tests smoke` ‏5/5، ‏`cfgcheck` PASS، و`MANIFEST-md5` ‏823/823.
7. **`make -C tests ce` أحمر سابقًا لقاعدة `main`**: فُحص بثنائيات مبنية من
   `9678765` (قاعدة الفرع) ومن الشجرة الحالية بحالة نظيفة (لا خوادم متسربة):
   السيناريو 1 لا يُسلّم الدفع للعميل المنتظر، وعدادات بوابات CacheEX الثلاث
   تبقى صفرًا في السيناريو 2. أُصلح عطل harness حقيقي واحد: هدف `ce` كان
   يستخدم `./.ncclient.bin` دون إعلانه اعتمادًا، فمن تشغيل نظيف كان العميل
   غائبًا أصلًا. أما فشل التسليم نفسه فسابق لكل التزامات هذه الجولة ويحتاج
   جلسة تتبّع ويب مخصصة (هل يفشل ECM فورًا لغياب مصدر قبل وصول الدفع؟) —
   لا يُدّعى إصلاحه هنا.
