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
7. **~~`make -C tests ce` أحمر~~ — أُغلق في هذه الجولة**: شُخّص من واجهة
   الويب/القياس الحي: الـECM كان يفشل بعد ‎~1‎ ثانية رغم `DCW TIMEOUT: 9000`
   في إعداد الاختبار، لأن `ecm_get_effective_timeout()` كان يُرجع
   `fallback_timeout` من جدول المزوّدين (1884:000000 ‎=‎ ‎1000ms‎) متجاوزًا
   مهلة المشغّل الصريحة دائمًا. الإصلاح: علم `dcw.timeout_explicit` جديد
   (نفس نمط `server.timeout_explicit` في D111 A1) — المهلة الاحتياطية للمزوّد
   تملأ الفراغ فقط عند غياب إعداد صريح. النتيجة: `make ce` أخضر بالكامل
   (3 سيناريوهات، بوابات LOCAL_ONLY/BLOCK_FAKE_CW/CWCHECK تعمل وعدّاداتها
   صحيحة)، و`cy` و`prof` و`cache-consensus-three-peers` و`smoke` و`cfgcheck`
   و`make-x64 test` ‏1153/0 بلا انحدار. ملاحظة: هذا يغيّر سلوكًا كان مقبولًا
   عند تفعيل جدول المزوّدين (9692eb8) — المهل الاحتياطية لم تعد تختصر مهلة
   صريحة؛ من أراد مهلة المزوّد يترك `DCW TIMEOUT` دون ضبط.
8. **جولة استقرار 2026-11-14 (ثانية)**: صيد منهجي في `tests/stability.mk`
   كشف ثلاث فئات: (أ) أهداف (`ha`/`xs`/`dl`/`jm`/`pm`/`cn`/`vl`) كانت تستخدم
   `.cachepeer.bin`/`.ncclient.bin` دون إعلان اعتمادات بنائهما — فشل صامت من
   شجرة نظيفة بسجلات مضللة؛ أُضيفت الاعتمادات. (ب) كشف `xs` كان يطابق
   `<script>` العام فيطابق سكربتات الواجهة ذاتها — ضُيّق إلى حمولة
   `<script>alert` الفعلية دون إضعاف البوابة (الاسم يظهر مهربًا
   `&lt;script&gt;` فعلًا). وهدف `pm` (أداة قياس بلا تأكيدات) كان يفشل من
   `pkill` أخير بلا مطابق — أُضيف `true`. (ج) انحدار منتج حقيقي كشفه `vl`:
   بوابة Nagravision في `setdcw.c` (أُضيفت لهذا الفرع لتنبيه 1814) كانت
   ترفض أي CW فاشل checksum لـCAID ‏1800–1884 حتى مع
   `DCWFILTER CHECKSUM: OFF` الصريح، فتتجاوز سياسة المشغّل بصمت؛ صارت
   تحترم التسلّح (profile `ON` أو `INHERIT` مع عام مُفعّل) وتبقى سندًا
   مُتتبَّعًا للمسلّحين. النتيجة: `jm` ‏8/8، `ha` ‏12، `dl` ‏7، `xs` ‏5،
   `cn` ‏7، `vl` ‏7/7، `pm` قياس ناجح، و`ce`/`fz`/`at`/`cy`/`smoke`/`prof`/
   `cfgcheck`/`make-x64 test` بلا انحدار. ملاحظة: مفاتيح P3 في `vl` مزوّرة
   عمدًا ولا تُسلَّم إلا مع CHECKSUM: OFF — وهو قصد الاختبار الأصلي.
9. **تدقيق شامل 2026-11-14 (لا عيّنات)**: بُنيت كل وحدات الترجمة بأعلام
   الإنتاج ثم بـ`-Wall -Wextra` وبـ`-fanalyzer`، وشُغّلت مجموعة `tests`
   كاملة هدفًا هدفًا (نحو 70 هدفًا) بعد أن كان `all` يتوقف عند أول أحمر.
   الإصلاحات: (أ) `main.c`: متغير `scored` في `cwentropy_note` كان يُقرأ
   غير مهيأ عند `report==0` — هُيّئ. (ب) `srv-camd35.c`: خيط `void*`
   يسقط عن نهايته عند `prg.restart` — أُضيف `return NULL`.
   (ج) `httpserver.c`: ‏55 موقع `sprintf` غير محدود أثبت GCC إمكان فائضه
   حُوّلت إلى `snprintf(sizeof)`. (د) حراس NULL عند مواقع malloc أثبتها
   المحلل (`config.c` ×4، `base64.c` ×2، `telnet.c`، `httpserver.c`)
   وتهيئة `buf` في `des.c` وحراس `cfg_addcachepeer`. (هـ) كسر بناء حقيقي:
   `srv-cccam.c` كان يستخدم أعضاء `CACHEEX` فقط دون حراس — البناء بلا
   CACHEEX (نسخة `cache-cross-provider` الثانية) كان مكسورًا؛ حُرست
   المواقع الثلاثة. (و) انحراف harness أخفى تحققًا حقيقيًا: حجم
   `struct cache_data` نما 102→103 بايت فصار مسبار `ca-bite` لا يعضّ
   أبدًا (الحجم الفعلي 105472 قِيس حيًا) وتأكيدا الحجم في
   `cache-cross-provider.c` (102/86→103/87)؛ وروابط ASan/المسبارات
   (`ps`/`xe`/`fl`) كانت تنقصها `-lm` منذ إضافة `sqrt` في رؤوس R48.
   (ز) التبرع: مسح كامل للمستودع والثنائيات — `/donate` غائب من الكود
   والمسارات والقوائم والاختبارات والثنائيات؛ لا بقايا إلا ذكر تاريخي
   في سجل القرارات. الباقي بعد الفرز: تحذيرات unused/sign-compare/
   misleading-indentation رُوجعت وتبيّن أنها حميدة (مشابك clamp مقصودة،
   فحوص <0 ميتة على حقول موجبة)، وبقية TODO تراثية من المنبع.
   التحقق: `make-x64 test` ‏1153/0، وكل أهداف `tests` خضراء فرادى بما
   فيها `ca` 9/9 و`cache-cross-provider` بنسختيه و`ps`/`xe`/`fl` بعد
   الإصلاح. استثناء بيئي سابق: `make all` في `src/Makefile` يطلب سلسلة
   tuxbox متقاطعة على مسار ثابت `/root/Desktop/...` غير موجودة — فشل
   أدوات لا كود. لم يُشغَّل `soak` في هذه الجولة.
10. **إغلاق دين القياس 2026-11-14**: صفحتا `/cache-analytics` و`/diagnostics`
    كانتا تعرضان أصفارًا دائمة (`cache_analytics_record_request` بلا أي
    مستدعٍ، وعدّادات diagnostics بلا كاتب). صارتا تُغذّيان من نقطة التسليم
    الوحيدة `trusted_deliver` في `ecm_setdcwdata` (نوع المصدر cache/server
    وزمن الاستجابة منذ `ecm->recvtime`) ومن مواقع الرفض المُتتبَّعة الأربعة
    (تحقق البروفايل، بوابة Nagravision، STRICT، SSF). أُضيف `diag_lock`
    ورقي في `diagnostics.c` (لم يكن فيها أي قفل)؛ ترتيب الأقفال
    `lockecm -> leaf` أحادي الاتجاه وقارئات الصفحات لا تأخذ `lockecm` فلا
    انعكاس. تغذية استشارية فقط — لا بوابة تعتمد عليها. التحقق: هدف جديد
    `tests tm` ‏6/6 (الصفحتان عند الصفر قبل الحركة وتتحركان بعد تسليم كاش
    حقيقي) أُضيف إلى `tests all`، و`make-x64 test` ‏1153/0، وsoak كامل
    ‎1200s‎ PASS (‏1128 ECM، ذروة RSS ‏6464 kB مستقرة) على ثنائي الجولة
    السابقة + soak قصير ‎90s‎ PASS على الثنائي الجديد.
    الديون الباقية كما هي: DoubleCheck بلا تحقق فعلي (قرار ميداني)،
    `cachewarm_tick` بلا مستدعين وإحماء ECM تغيير بروتوكولي، جدول
    parallel-ECM ميت، `web_dashboard` بديل معطّل عمدًا، والبوابات الميدانية
    تحتاج بطاقات/نظراء حقيقيين.
11. **إحياء memcheck‏ 2026-11-14**: هدف `make-x64 memcheck` (ASan+UBSan
    وTSan وفالغرايند على الوحدات وجلسة حية) كان مكسورًا من خمس طبقات
    انحراف harness: فقدان بتّ تنفيذ السكربت، غياب `-lm` عن رابط الخادم،
    إعادة كتابة `-o` لا تفهم قواعد البناء بخطوتين (scoped-policy)، تلوث
    مجلد كائنات الخادم بكائنات الوحدات، وقائمة مصادر خادم يدوية تخلّفت
    ‎+20‎ وحدة ترجمة (صارت تُشتق آليًا من `OBJECTS` في `src/Makefile`)،
    و`-std=gnu89` من طبقة المنفذ ترفض حلقات C99 التي يقبلها البناء
    الحقيقي (استُبدلت بـ`-fgnu89-inline`). أُضيف كبت LSan موثق ومقصور على
    مكدّس `read_config` (رسم الإعدادات ملكية عمر-عملية بالتصميم) وسكربت
    يتخطى فالغرايند بصراحة عند غياب الأداة بدل 88 فشلًا زائفًا.
    النتيجة: `MEMCHECK: clean` — وحدات ASan+UBSan ‏88/88، TSan ‏88/88،
    جلسة ASan حية نظيفة، جلسة TSan حية تعمل مع 11 سباقًا من صنف D49
    الموثق سلفًا (استثناء معروف، لا يُخفى ولا يُدّعى إصلاحه هنا).
    فالغرايند غير مثبّت في هذه البيئة فتُركت أجزاؤه متخطاة بصراحة.
    وفُرزت فئة `-Wsign-compare` كاملة (25 موقعًا): كلها مقارنات مساواة
    بمعرّفات/عدّادات غير سالبة ومكمّلات مقصوصة — حميدة، بلا تغيير كود.
    الدوال «غير المستخدم» الـ59 كلها واجهات رؤوس مشتركة (لا ميت في ‎.c‎).
12. **تدقيق أقفال مستقل 2026-11-14 (D49)**: فُحصت تقارير TSan الحية
    ‎12‎ واحدًا واحدًا بمكدّساتها الكاملة لا بملخصاتها. ثلاثة أصناف:
    (أ) ‎8‎ سباقات على `cfg` في نافذة الإقلاع: خيط الإعدادات كان ينشر
    `mark_config_ready()` بعد التحليل ثم يشغّل `check_config()` (فتح كل
    مقابض الاستماع) و`cfg_set_id_counters()` بعد ‎~100ms‎ بينما خيوط
    HTTP/الخوادم تقرأ المقابض والمعرّفات — إكمالٌ لقصد TASK 4.1: صار
    الترتيب كممرّ إعادة التحميل (معرّفات ثم مقابض) وكله قبل النشر،
    وحُذفت نومة «الأمل». (ب) سباقات خانات `prg.tid_*`: خيوط تكتب
    `pthread_self()` في خانة يكتبها `create_thread` أصلًا لخيط لاحق في
    الخانة نفسها — حُذفت الكتابات الذاتية في `th-ecm.c`‏ ×2 و
    `clustredcache.c`‏ ×2 (الخانات تشخيصية ولا تُقرأ أبدًا).
    (ج) مؤشر حلقة التصحيح `idbgline` يُزاد بلا تزامن — صار يُدّعى بـCAS
    ملفوف مع قراءات ذرية في صفحة debug وtelnet؛ محتوى الخانات يبقى حلقة
    حرة القفل بالتصميم كما كان.
    النتيجة المقاسة: جلسة TSan الحية من ‎11–12‎ سباقًا إلى **0**،
    و`MEMCHECK: clean`، و`make-x64 test` ‏1153/0، وأهداف smoke/no-profile/
    newcamd-config-reload/au/sk/ce/vl/jm/ca/cn خضراء. المتبقي بصراحة:
    سباقات محتوى حلقة التصحيح (قراءة سطر أثناء كتابته — مقبولة تصميميًا)
    وأي سباق لم تثره جلسة حية قصيرة؛ لا يُدّعى خلوٌّ شامل.
13. **جلسة TSan حيّة مُثقلة 2026-11-14 (متابعة D49)**: الجلسة القصيرة في
    البند 12 لم تكن كافية وحدها. بُني خادم TSan كامل من كل وحدات
    `OBJECTS` وأُخضع لحمل متزامن: عميلا Newcamd (ECM مستمر)، نظيرا كاش
    (أحدهما بمفتاح فاسد عمدًا)، تصفح كل صفحات HTTP مرارًا، جلسات Telnet
    ناجحة وخاطئة، محاولات HTTP خاطئة، وإعادة تحميل بالإعدادات عبر
    inotify — ومنها إعادة تحميل **تغيّر منفذ HTTP فعليًا** ذهابًا وإيابًا
    أثناء ضرب حلقة القبول بطلبات متصلة. أثارت الجلسة ما فات القصيرة:
    (أ) قراءة `cfg.http.handle`/`cfg.telnet.handle` في حلقتي قبول
    HTTP/Telnet مقابل كتابة `check_config` عند تغيّر المنفذ — صارت
    الحلقات تلتقط المقبض ذرّيًا مرة واحدة كل دورة، وكل كتابات الإعدادات
    للمقابض ذرّية. (ب) `strcpy` بيانات اعتماد Telnet في `reread_config`
    مقابل `strcmp` في `telnetprocess` — صار مسار Telnet يلتقط الاعتماد
    وقائمة السماح وتأخير الدخول تحت بوابة الإعدادات نفسها ويقارن النسخ
    (مسار HTTP كان محميًا أصلًا بقفل قراءة الإعدادات).
    التحقق بنفس السيناريو حرفيًا بعد الإصلاح: 120 ECM مُسلّمة،
    إعادتا تحميل بتغيّر منفذ، **0 سباقات TSan** في سجل stderr كامل غير
    مقتطع؛ وقبل الإصلاح سجّلت الجلسة نفسها 4 تقارير (تقرير لكل اتجاه).
    `make-x64 test` ‏1142/1142 و`MEMCHECK: clean` واختبار
    newcamd-config-reload أخضر. **ديون مُسمّاة لا تُخفى**: بقية حقول
    `cfg` تُقرأ في مواضع أخرى دون بوابة وتُعاد كتابتها عند التحميل —
    أُصلح المُثبت بمكدّسات كاملة فقط، وأي ادعاء خلوّ شامل يحتاج جلسات
    أطول ومسارات أكثر (بطاقات/خوادم CCcam حقيقية). كذلك: DoubleCheck
    ما زال لا يتحقق فعليًا (قرار ميداني)، `cachewarm_tick` بلا مستدعين،
    جدول parallel-ECM ميت، `web_dashboard` معطّل عمدًا في `main.c`،
    وفالغرايند غير مثبّت هنا فتظل أجزاؤه متخطاة بصراحة.
14. **جلسة TSan حيّة شاملة المسارات 2026-11-14 (متابعة D49)**: بُني خادم
    TSan كامل وشغّل بإعداد يُشعل كل المستمعات المتاحة في هذه النكهة معًا:
    Newcamd وHTTP وTelnet وكاش (نظيران، أحدهما بمفتاح فاسد) وCCcam
    (عميل ccdcw ببطاقة ncserver وهمية عبر N:) وMGcamd وcamd35‏ (UDP)
    وcs378x وFreeCCcam (فُعّل لاحقًا بإعادة تحميل) — حركة متزامنة: ECM
    من مستخدمين، ECM عبر CCcam، كل صفحات HTTP، Telnet صالح/خاطئ، مسابر
    بروتوكولية خام (إطارات MGcamd/cs378x فاسدة وقطع مفاجئ، داتاغرامات
    camd35)، وإعادتا تحميل بتغيّر منفذ أثناء الحمل. النتيجة قبل
    الإصلاح: **36 تقرير سباق في 5 أصناف**، فُرزت كلها بمكدّساتها:
    (أ) `ip2string` — تناوب 3 مخازن ثابتة مشتركة بعدّاد غير متزامن
    (مسارا mgcamd/newcamd يفسد أحدهما سلسلة الآخر): صارت المخازن
    thread-local بنفس عقد التناوب. (ب) محتوى حلقة `dbgline`: قفل
    `dbg_ring_lock` حول نسخ الكتابة والقراءة عبر `dbg_copy_line()` —
    القارئ يأخذ سطرًا كاملًا لا ممزقًا؛ الادعاء السابق «حلقة حرة القفل
    بالتصميم» أُلغي لأن الإصلاح أرخص من الدين. (ج) عدّادات البروفايل
    `ecmaccepted/ecmdenied/ecmok/ecmoktime/ttime*`: كتّابها (30 موقعًا في
    setdcw وخيوط الخوادم) صاروا `__atomic_fetch_add` وقراءات الصفحات
    `__atomic_load_n`. (د) `localtime()` في `th-date` (خيط انتهاء
    الصلاحية): كشفت ASan أن المشكلة أعمق — البناء بـ`-fpack-struct`
    يجعل `struct tm` أصغر مما تكتبه libc، فحتى `localtime_r` المحلي
    يفيض 4 بايتات؛ الحل بنيوي: دالة `mcs_localtime_fields` في وحدة
    safe-time ذات التخطيط الأصلي مع مخزن بذيل احتياطي يعمل في كل
    النكهات (بما فيها نكهتا memcheck)، وصفحة BISS صارت تستخدم
    `mcs_format_local_time`. (هـ) خيط انتهاء الصلاحية يجول قوائم عملاء
    CCcam/MGcamd بينما `update_cccam_clients` يعيد كتابتها عند التحميل:
    صار الفحص يمسك بوابة الإعدادات (ترتيب أقفال موثق أحادي الاتجاه).
    بعد الإصلاح وبنفس السيناريو حرفيًا: **0 سباقات** (120 ECM وما
    فوقها بين المسارين، إعادتا تحميل، كل المسابر)؛ `make-x64 test`
    ‏1142/1142، `MEMCHECK: clean`، وnewcamd-config-reload أخضر.
    **قيود وديون باقية بالاسم**: (1) خادم Radegast غير مُترجم في هذه
    النكهة (`RADEGAST_SRV` اختياري وقت البناء) فلم يُثَر حيًا — أُثير
    مسار عميله في جلسات سابقة فقط؛ (2) عدّادات `srv->`/`cli->` الإحصائية
    من نفس صنف (ج) ولم تُحوّل بعد (إحصاءات عرض، لا قرارات)؛ (3) بقية
    حقول `cfg` غير المحمية بالبوابة في قراءات متفرقة — أُصلح المُثبت
    بمكدّسات فقط؛ (4) `cachewarm_tick` طبقة بلا مستدعين وجدول
    parallel-ECM يُهيّأ ولا يُستخدم و`web_dashboard` معطّل عمدًا في
    `main.c` — كود ميت موثق، حذفه قرار مستقل؛ (5) DoubleCheck لا
    يتحقق فعليًا (قرار ميداني)؛ (6) فالغرايند غائب فتُتخطى أجزاؤه
    بصراحة، ولا تحقق ميدانيًا ببطاقات/نظراء حقيقيين لأي مسار.

15. **جولة «إضافة الكل» (2026-11-14)**: نفّذت قرار إضافة الطبقات الميتة
    (48 رأسًا بلا مستدعٍ إنتاجي وقت الجرد) دفعةً دفعةً مع تحقق كل دفعة
    (بناء `release`، وحدات 1142/1142، وأخيرًا memcheck نظيف بلا سباق
    واحد)، بلا أي تغيير في سلوك الإعداد الافتراضي: كل طبقة مربوطة
    إمّا خلف نكهة بناء `release-r22` (الماكرو R22_LAYERS، افتراضيًا
    غير معرّف) أو وضع مراقبة بلا تأثير قرار. (أ) web-dashboard:
    الوحدة المستقلة حُذفت نهائيًا (مقبس 8080 بلا مصادقة وبلا تغذية
    إحصائيات واحدة — تفعيلها كما هي كان يعني صفحة أصفار على منفذ غير
    محمي)؛ شكلها العربي RTL حيٌّ الآن كصفحة `/dashboard` داخل خادم
    HTTP الحالي خلف مصادقته (`http_send_dashboard_page`) ومغذّاة من
    عدّادات البروفايل الذرية الحقيقية. (ب) r22layers: المظلة مُضمّنة
    الآن في `main.c` و`r22_layers_init()` تُستدعى عند الإقلاع، وخطّافا
    `r22_hook_cache_setdcw` (في `cache_setdcw`) و
    `r22_hook_ecm_setsrvflagdcw` (في `setdcw.c`) مدمجان فعليًا؛ خطّافا
    `cw_failed/cw_verified` تُركا غير مربوطين عمدًا — لا توجد رسالة
    بروتوكولية تُعلم الخادم أن عميلًا فكّ التشفير فعليًا، وربطهما عند
    التسليم كان سيخترع دليلًا. (ج) DoubleCheck اكتمل برمجيًا: أُزيلت
    CAIDs المخترعة الثمانية من `load_defaults`، وأضيف سطر إعداد
    `ECM DOUBLECHECK: 0500 1884...` (سداسي، حتى 64 CAID) في config.c
    يُطبق عند الإقلاع (بعد `cache_speed_suite_init`) وعند إعادة
    التحميل في th-cfg.c، وكل CW ملتزم يُغذّى إلى الـpending المطابق
    بالهاش عبر `add_result` في setdcw.c مع سجل verified/MISMATCH —
    **وضع مراقبة فقط، لا يحجب التسليم أبدًا**؛ اختبارات الوحدة حُدّثت
    ونُقل فحص امتلاء الجدول (256) لسياقه الصحيح فشغلت الوحدات
    1142/1142. (د) parallel-ECM حذف: جدول `parallel_tab` كان يُهيّأ
    في main() ولا يُقرأ/يُكتب في أي مسار؛ أُزيلت واجهته وتهيئته
    (الحيّة من advanced-optimizations: Auto-Recovery وCW-encryption و
    predictive warming بقيت). (هـ) الدين (2) من البند 14 سُدّد:
    `ecmok/ecmoktime` على srv/cli/cards صارت ذرّية كتّابًا وقرّاءً
    (__atomic_fetch_add / load_n، حقول int دون تغيير ABI).
    **ديون باقية بالاسم**: (و) حقول cfg المتبقية غير المحمية بالبوابة
    في قراءات متفرقة — قياس الخطر وحدةً وحدةً قبل أي تحويل؛ (ز)
    Radegast server ما زال خارج نكهة البناء (RADEGAST_SRV اختياري
    وقت البناء)؛ (ح) فالغرايند غائب فتُتخطى أجزاؤه بانتظام؛ (ط)
    DoubleCheck مراقبة لا حجب — تحويله إلى حجب قرار تشغيلي يحتاج
    رصد mypy للمطابقة الميدانية، لا قرار كود؛ (ي) جلسة TSan للنكهة
    r22 المبنية بماكرو الطبقات لم تُدار بعد — التحقق الحي الحالي
    للنكهة هو صفحة /dashboard ترد 200 بلا أخطاء سجل.

16. **التفعيل الميداني (2026-11-14)**: بطلب «نحن سنجربه ميدانيًا، فعّل
    كل ما يفيد المستخدم» صارت الطبقات النافعة مفعّلة فعليًا في نسخة
    التجربة مع مفتاح هروب فوري، بدل بقائها opt-in نظرية: (أ) ثنائي
    `release-r22` هو ثنائي التجربة الميدانية — الطبقات مجمّعة فيه
    ومفعّلة افتراضيًا، مع سطر إعداد جديد **`R22 LAYERS: YES/NO`**
    (يُقرأ في كل النكهات بلا ضرر) يوقف الخطافات فورًا عند إعادة
    التحميل دون إعادة بناء؛ بوابات الخطافات والتهيئة تفحص
    `cfg_r22_layers_enabled` عند كل دخول. (ب) DoubleCheck صار يُسلَّح
    تلقائيًا من CAIDs التي عرّفها المشغّل أصلًا على بروفايلاته عند
    غياب سطر صريح (لا CAID مخترعًا)، والسطر الصريح يفوز دائمًا،
    و`ECM DOUBLECHECK: OFF` يعطّله نهائيًا؛ يبقى وضع مراقبة وتسجيل
    (verified/MISMATCH) لا يحجب أي تسليم. (ج) صفحة `/dashboard` تعرض
    الآن حالة الطبقات وعدد CAIDs المراقبة لحظيًا. تحقق حي على ثنائي
    r22: الطبقات «مفعّلة» وDoubleCheck مسلّح لـ1 CAID (1884 من
    البروفايل دون أي سطر إعداد)، وبإعداد الإيقاف: «موقوفة» و0 CAID،
    وثنائي stock يقرأ السطرين بلا ضرر ويعرض «غير مُجمّعة». الوحدات
    1142/1142 و`MEMCHECK: clean` (0 سباقات). **يبقى بالاسم**: خطافا
    cw_failed/cw_verified بلا ربط (لا دليل بروتوكولي على فكّ تشفير
    العميل)، وDoubleCheck لا يحجب (قرار تشغيلي بعد رصد المطابقة
    ميدانيًا)، وجلسة TSan خاصة بنكهة r22 تحت حمل ECM حقيقي لم تُدر
    بعد — جلسة memcheck الحالية تغطي نكهة stock.

17. **صفحة واحدة لكل شيء (2026-11-14)**: بطلب المشغّل ألا يتنقّل بين
    `/dashboard` وسجلات `[ECM-DBLCHK]`، صارت صفحة CCcam Trace
    (`/user-trace`) هي المكان الوحيد: قسم «نظرة عامة» جديد في أعلى
    قسمها المُحدّث تلقائيًا كل 5 ثوانٍ (`trace_overview_write`) يعرض
    حالة النظام ووقت التشغيل وجدول الباقات بنسب نجاحها، وعدّادات
    طبقات R22 الحيّة (مفحوص/مقبول/مرفوض بأسبابه: فايك واضح، متعدد
    قنوات، قائمة حظر، محتجزة بالحجر، حقائق البطاقات)، وقسم
    DoubleCheck بعدّاداته وآخر قراراته (تطابق مصدرين ✅ / عدم تطابق
    ⚠️ مع القناة والوقت وعدد المصادر). القرارات تُحفظ الآن في حلقة
    ذاكرة (32 قرارًا، الأحدث أولًا) داخل `ecm-doublecheck.h` بدل
    خطوط debugf فقط، مع 3 فحوص وحدة جديدة لها. لأن httpserver.o وحدة
    ترجمة منفصلة وحالة DoubleCheck/R22 statics في وحدة main، بُني
    جسر `src/field-status.h` (POD يملؤه `mcs_field_status_fill` في
    main.c) بدل تضمين الرؤوس في httpserver ورؤية أصفار صامتة؛
    وعدّادات r22stats صارت زياداتها ذرّية. صفحة `/dashboard` أُزيلت
    (دالة ومسار ورابط قائمة) ومسارها القديم يحوّل 303 إلى
    `/user-trace`. تحقق: بناء stock وr22، وحدات 1142/1142 (منها
    DoubleCheck ‏27/27)، وحي على r22: الصفحة تعرض النظرة العامة
    كاملة والطبقات مفعّلة وCAIDs مراقبة: 1، والتحويل يعمل،
    و`MEMCHECK: clean` (0 سباقات). **يبقى**: قرارات DoubleCheck لا
    تظهر إلا عند بدء فحوص على CAIDs مسلّحة وحركة ECM حقيقية —
    الصفحة تقول ذلك صراحة عند الفراغ بدل ادعاء نتائج.

18. **DoubleCheck يعمل حيًا بقرارات حقيقية (2026-11-14)**: اختبار حي
    جديد `tests/doublecheck-live.py` (هدف `make -C tests
    doublecheck-live`) بخادمي Newcamd تركيبيين (بطيء 2500ms وسريع
    60ms، و`SERVER INTERVAL: 200`/`SERVER MAX: 2` فيجعله الخادم يرسل
    الـECM نفسه لبطاقتين). كشف الاختبار الأول فجوتين حقيقيتين
    أُصلحتا: (1) كل إرسال كان يفتح pending جديدًا بدل الانضمام
    للموجود (العدّاد أظهر «فحوص بدأت: 2» لقرار واحد) — صار
    `ecm_doublecheck_start` يعيد الـpending النشط لنفس الـhash؛
    (2) الـCW الثاني المتأخر كان يُسقط قبل الالتزام فلا يصل أي قرار
    أبدًا — صارت التغذية عند **وصول** CW الخادم في
    `ecm_setsrvflagdcw` (ecmdata.c، كل بروتوكولات البطاقات الخمسة)
    عبر جسر `mcs_doublecheck_feed` إلى وحدة main، مع بقاء تغذية
    الالتزام؛ و`add_result` صار يخصم لكل قارئ مرة واحدة حتى لا
    يُختلق «تطابق مصدرين» من مصدر واحد يُغذّى مرتين (وصول+التزام).
    النتيجة حية على ثنائيي r22 وstock: وضع agree يعرض «✅ تطابق
    مصدرين» ووضع mismatch يعرض «⚠️ عدم تطابق المصادر» في صفحة
    `/user-trace`، والعميل يستلم DCW في الوضعين (المراقبة لا تحجب).
    فحوص الوحدة 31/31 لـDoubleCheck والمجموع 1142/1142،
    و`MEMCHECK: clean` (0 سباقات). **يبقى**: القرار يحتاج مصدرين
    فعليين لنفس الـECM (بطاقتين، أو بطاقة+كاش) — ببطاقة واحدة لن
    يظهر قرار، وهذا صحيح لا عطل؛ وتحويل عدم التطابق إلى حجب يبقى
    قرارًا تشغيليًا بعد رصد ميداني.

19. **جلسة TSan للنكهة R22 تحت حمل ECM وكاش (2026-11-14)**: بُني
    الخادم بـ`-fsanitize=thread -DR22_LAYERS` وشُغّل عليه اختبار
    البطاقات الثنائية (الوضعان) وجلسة كاش CSP. النتيجة: سباق واحد
    مُثبت في كود stock لا في الطبقات — `recv_msg_thread`
    (th-ecm.c:1110) يقرأ `srv->handle` تحت بوابة الإعدادات فقط بينما
    عامل الاتصال يكتبه تحت `prg.locksrv` (cli-newcamd.c:192)؛
    أُصلح بتحميل ذرّي واحد في قارئ الـpoll وتخزين ذرّي عند كل
    كتّاب الاتصال في بروتوكولات cli-*. بعدها: **0 سباقات** في
    الوضعين، و0 سباقات تحت قصف الكاش (258 CW عولجت عبر خطاف
    الكاش)، والوحدات 1142/1142 و`MEMCHECK: clean`. اكتشاف سلوكي
    مصاحب: طبقة «متعدد القنوات» رفضت 254/258 من CWs نظير تركيبي
    يعيد استخدام CW عبر قنوات — رفض مفرط لأشكال حركة موجودة
    ميدانيًا، فحُوّلت إلى **مراقبة فقط** (تعدّ الاشتباه على صفحة
    التريس ولا ترفض ولا تسمّم قائمة الحظر)؛ يبقى الرفض الغاشم
    لأنماط الفايك الواضحة وقائمة الحظر فقط. إعادة تسليحها بوابةً
    تحتاج بيانات ميدانية. **يبقى بالاسم**: فشل `cache-trace-network`
    في حالة static (نص سياسة انتظار) يحدث في ثنائي stock أيضًا —
    فشل harness سابق/بيئي لم يُشخّص جذره بعد، ولم يُنسب لهذه
    الجولة؛ بقية قارئي `srv->handle` خارج مسار الـpoll لم يُحوّلوا
    (لم يثبت سباقهم بمكدّس)، والتحويل الشامل قرار مستقل.

20. **تشخيص فشل `cache-trace-network` ‏(static) وإغلاقه (2026-11-14)**:
    الفشل الذي سُمّي «سابق/بيئي» في البند 19 شُخّص جذره: الـharness
    كان يتوقع `ECM thread timeout=500` (‏preset الـCAID 1884) مع
    `CACHE TIMEOUT: 900` صريح في البروفايل، بينما سياسة D111 A1
    الموثقة في `cache-window.h` والمفحوصة وحدويًا في
    `test_cache_evidence_config` تقول إن قيمة المشغّل الصريحة تغلب
    الـpreset — والسلوك الفعلي المقاس حيًا 900/900 كما صُمم. إذن
    التوقع كان خاطئًا لا السياسة: صُحّح في الـharness مع تعليق يشرح
    A1، والآن الشبكة كاملة **17/17 على ثنائيي stock وr22** (كانت
    تتوقف عند آخر حالة). الوحدات 1142/1142 و`MEMCHECK: clean`.
    درس مُسجل: عندما يتعارض harness مع وحدات موثقة، يُقاس السلوك
    الحي أولًا ثم يُصحّح الجانب الخاطئ — لا تُعدّل سياسة موثقة
    لإرضاء توقع قديم.

21. **جلسة TSan موسعة للنكهة R22 صارت harness دائمًا (2026-11-14)**:
    `tests/tsan-r22-session.sh` (هدف `make -C tests tsan-r22`) يبني
    الخادم بـ`-fsanitize=thread -DR22_LAYERS` ويقوده بستة سيناريوهات
    شبكية موجودة (بطاقتا DoubleCheck، تريس CCcam، شبكة الكاش 17
    حالة، CCcam كبطاقة upstream، هوية المزود، إعادة تحميل الإعداد)
    مع التقاط تقارير TSan عبر `log_path` (مخرجات الخادم تضيع داخل
    مجلدات الـharness المؤقتة، وlog_path لا يضيع). أول تشغيل كامل:
    **6/6 سيناريوهات خضراء و0 تحذيرات سباق** — طبقات R22 ومسارات
    وصول/التزام DoubleCheck وإعادة التحميل نظيفة تحت التجهيز في
    هذه السيناريوهات. **يبقى بالاسم**: هذا لا يثبت خلو بقية
    المسارات (Radegast خادمًا خارج النكهة أصلًا، ومسارات EMM
    وFreeCCcam لم تُثَر هنا)، وقارئو `srv->handle` خارج مسار الـpoll
    لم يُحوّلوا لعدم ثبوت سباقهم بمكدّس حتى الآن؛ الجلسة الجديدة
    تجعل إعادة الفحص أمرًا واحدًا عند أي تغيير مقبل.

22. **مسار FreeCCcam يُثار أول مرة وسباقه يُصلح (2026-11-14)**: لم
    يكن لـFreeCCcam أي harness شبكي (ذكره الوحيد فحص إعداد)، فبُني
    `tests/freecccam-live.py` (هدف `make -C tests freecccam-live`)
    واكتُشفت بالتجربة متطلبات مصافحته غير الموثقة: خانات العملاء من
    `FREECCCAM MAXUSERS`، وCLI_INFO بطول 93 بايت (وضع
    `CCDCW_CLIINFO97` جديد في ccdcw)، وحل البروفايل برقم البطاقة
    الذي يبدأ من 0x64 (`CCDCW_CARDID`) — والمسار يعمل طرفًا لطرف
    (DCW خلال 92ms). أضيف كسيناريو سابع لجلسة TSan فكشف فورًا سباقًا
    حقيقيًا: `frcc_recvmsg_thread` يقرأ `cli->handle` وعامل الاتصال
    يكتبه وكلاهما تحت بوابة قراءة الإعدادات نفسها (قفل قراءة لا
    يستبعد القارئين)؛ أُصلح بتخزين/تحميل ذرّي كما في صنف
    `srv->handle`. النتيجة: **7/7 سيناريوهات، 0 سباقات**، والوحدات
    1142/1142 و`MEMCHECK: clean` وثنائي r22 يجتاز freecccam-live.
    **يبقى بالاسم**: مسار EMM بلا أي مرسل اختبار في الشجرة كلها —
    إثارته تحت TSan تحتاج fixture مرسل EMM أولًا (عمل بروتوكولي
    مستقل، لم يُخترع هنا)، وخادم Radegast خارج نكهة البناء بنيويًا.

23. **حقيقة EMM وتصحيح ادعاءي إقلاع كاذبين (2026-11-14)**: طلب بناء
    مُرسل EMM للاختبار قاد لفحص المسار كاملًا، والنتيجة بنيوية: هذه
    النكهة لا تستقبل EMM أصلًا من أي بروتوكول — `CC_MSG_EMM_REQUEST`
    معرّف بلا معالج، وبروتوكول Newcamd هنا بلا رسالة EMM، وأوامر
    camd35/cs378x تعليقات فقط، و`cache_speed_on_emm` بلا مستدعٍ واحد،
    والنداء الحي الوحيد لـ`ecmpat_analyze` يمرّر «not EMM» دائمًا.
    إذن مُرسل اختبار كان سيختبر فراغًا، وإثارة المسار تحت TSan
    مستحيلة دون تنفيذ استقبال EMM في الخادم أولًا — أي ميزة
    بروتوكولية جديدة، خارج قاعدة «لا تعديل بروتوكولي» وقرارها
    للمشغّل لا للكود. ما أُصلح فورًا هو الكذب التشغيلي: سطرا
    الإقلاع `[EMM-CACHE] active` و`[EMM-SHARED] active` صارا يقولان
    صراحة إنهما inert ولماذا (تحقق حي من سجل الإقلاع)، كما أُزيل
    سطر «8 CAIDs» الكاذب في جولة سابقة لنفس السبب: سجلات الإقلاع
    وثيقة مشغّل، لا إعلان. الوحدات 1142/1142 و`MEMCHECK: clean`.
    **قرار مطلوب من المشغّل إن أراد EMM فعليًا**: تنفيذ الاستقبال
    (على الأقل عبر CCcam حيث الرسالة معرّفة) مشروع مستقل بتصميم
    وتوافق عملاء حقيقيين، لا يُفتح ضمن جولات التفعيل هذه.

24. **إغلاق صنف سباق `srv->handle` شاملًا (2026-11-14)**: دين بند 19
    بقي أن التحميل الذرّي وُضع في موضع الإثبات فقط (th-ecm) بينما
    بقيت قراءات عادية كثيرة لنفس الحقل يفترشها الناشرون الذرّيون —
    وTSan يعدّ خلط قراءة عادية بكتابة ذرّية سباقًا أينما اشتعل.
    حُوّلت كل قراءات `srv->handle` في الشجرة (79 موضعًا عبر 17 ملفًا)
    إلى `__atomic_load_n(RELAXED)`؛ لا كتابة عادية متبقية إلا تهيئة
    `-1` عند الإنشاء قبل النشر. اصطُدم أثناء التحويل الآلي بفخ
    الاسم الفرعي: `newsrv->handle` (مستمعات أخرى) التُقطت كـ
    `srv->handle` فصُحّحت قبل أي بناء. وأُصلح عطل تجهيزي ظهر بنفس
    الجولة: جلسة TSan عند استدعائها عبر `make -C tests` ورثت
    `MAKEFLAGS` فتسرّبت سطور «Entering directory» إلى متغير أعلام
    الترجمة؛ صار الاستدعاء الفرعي يصفّرها مع
    `--no-print-directory`. النتائج: جلسة TSan **7/7 و0 سباقات**
    عبر هدف make نفسه، والوحدات 1142/1142، و`MEMCHECK: clean`،
    وfreecccam-live وcccam-trace أخضران على الثنائيات. يبقى بالاسم:
    `cli->handle` من جانب العميل في بروتوكولات الخادم المختلفة لم
    يُحوّل شاملًا (إثبات FreeCCcam غطّى طرفَي مساره، وبقية
    البروتوكولات بلا اشتعال ولا إثبات حتى الآن)، وحقول cfg المقروءة
    خارج البوابة في مواضع متفرقة كما هي منذ بند 14.

25. **تدقيق شامل: هل كل الخاصيات مفعّلة برمجيًا وعمليًا؟ (2026-11-14)**:
    لا — والتفصيل الكامل في `docs/AUDIT-BLACKSCREEN-CACHE-ar.md`.
    المنهج تتبّع كل محرك في 4 نقاط (تهيئة/مستدعٍ حي/قرار رفض/
    افتراضي) مقارنةً بأدوات OSCam المثبتة (cwcycle_check،
    cacheex_cw_check mode 2، dropdiffs، حظر r10919). العامل
    والرافض فعليًا في مسار THREAD_DCW: تحقق البروفايل، بوابة
    Nagravision المشروطة، فحص الدورة، وبوابات دخول الكاش
    (negative memory/attest/الإجماع عند التسليح)، وخطافا R22
    الفعّالان في ثنائي r22، مع سرعة حيّة من pipe_cache_find ونافذة
    D111 ومرشح cache_verify الاحتياطي. المراقبة فقط: DoubleCheck،
    بصمة CW الإحصائية، IBS (عقوبته ثقة لا يستهلكها رافض حي)، وبقية
    طبقات R22 (جداول بلا قارئ). الميت رغم إعلان الإقلاع: رافضو
    Cache-Intelligence/CW-POSTVAL/ECM-PATTERN وحزمة
    cache-speed وauto-tuning/predictive-warming (موجودة حصرًا في
    `#ifndef THREAD_DCW` أو بلا مستهلِك)، وداخل الميت خطر
    `ci_predict_fetch` المستبدِل للمفاتيح — لا يُحيا دون إعادة
    تصميم. النقائص مرتبة بالأثر داخل الوثيقة، والتوصيات قرارات
    تشغيل وبيانات ميدانية أولًا، لا كود جديد في هذه الجولة.

26. **تنفيذ توصيتي تدقيق بند 25 (2026-11-14)**: (أ) سطور إقلاع
    الأنظمة الميتة صارت صادقة (CACHE-SPEED وCW-POSTVAL/TS-VALIDATE
    وطبقات CACHE-AI السبع تصف نفسها inert/بلا مستهلِك، كما في بند
    23). (ب) نُقل `ci_validate_cw` منفردًا من مكدّس
    `#ifndef THREAD_DCW` الميت إلى المسار الحي خلف مفتاح جديد
    `CACHE CI VALIDATE: ON` (افتراضه OFF = stock byte-for-byte)،
    مع إصلاح سباق كامن ظهر عند النقل: فحص قاعدة الفايك المعروف
    كان يقرأ `g_ci.fake_cws` بلا قفل بينما كاتبها يمسك القفل —
    صار الفحص مقفلًا. بقية المكدّس الميت تُركت عمدًا، وخطر
    `ci_predict_fetch` المستبدِل للمفاتيح موثق كسبب عدم الإحياء.
    الإثبات الحي `tests/ci-validate-live.py` (هدف
    `make -C tests ci-validate-live`): نفس CW منخفض الإنتروبيا
    صالح الـchecksum يُسلَّم مع OFF ويُرفض مع ON — لا فرق آخر بين
    التشغيلين. وجلسة TSan ‏7/7 بصفر سباقات (تشمل قفل قاعدة الفايك
    الجديد)، والوحدات 1142/1142، و`MEMCHECK: clean`، والاختبار
    أخضر على r22 أيضًا. **فجوة ملاحظة جديدة بالاسم**: ECMs القادمة
    من عملاء FreeCCcam بلا trace instance، فرفض هذه البوابة لا
    يظهر في `/user-trace` (يظهر أثره سلوكيًا فقط) — سدّها يحتاج
    ربط تريس لمسار FreeCCcam، عمل مستقل لم يُنفذ هنا.

27. **سد فجوة تريس FreeCCcam (2026-11-14)**: رفض بند 26 لم يصل
    `/user-trace` لأن مسار FreeCCcam لم يربط التريس أصلًا، والفحص
    كشف طبقتين أعمق: قائمة اختيار العميل في الصفحة تعدّ عملاء
    CCcam فقط، وبدء التريس يتحقق بـ`getcccamclientbyid` الذي لا
    يرى عملاء FreeCCcam أيضًا — أي أن المشغّل لم يكن يستطيع
    تتبّع عميل FreeCCcam حتى لو أراد. الإصلاح بثلاث لمسات على
    نفس النمط القائم: (أ) srv-freecccam يبدأ محاولة تريس عند كل
    ECM ويعين `trace_instance` (انضمام/جديد/مرفوض) كما في
    srv-cccam حرفيًا؛ (ب) قائمة الصفحة تضم خانات
    `cfg.freecccam.server.client`؛ (ج) `getcccamclientbyid` يبحث
    في قائمة FreeCCcam أيضًا فيفيد كل مستدعيه. والاختبار الآن
    يثبت من الصفحة نفسها: مع `CACHE CI VALIDATE: ON` يفعّل
    الاختبار تريسًا من نموذج الصفحة لخانة FreeCCcam ثم يشترط
    ظهور «Cache Intelligence validation» في `/user-trace` —
    فصار الرفض مشهودًا من واجهة المشغّل لا من السلوك فقط.
    جلسة TSan ‏7/7 صفر سباقات (تشمل ربط التريس الجديد تحت حمل
    السيناريوهات)، والوحدات 1142/1142، و`MEMCHECK: clean`،
    وfreecccam-live وcccam-trace أخضران. **يبقى بالاسم**: معرّفات
    خانات FreeCCcam من عدّاد مستقل قد تصطدم عدديًا بمعرّفات
    عملاء CCcam، والتريس يطابق بالمعرّف وحده — توحيد فضاء
    المعرّفات عمل أوسع لم يُفتح هنا.

28. **توحيد فضاء معرّفات cc_client_data + سباق لقطة التريس (2026-11-14)**:
    خانات FreeCCcam كانت تُرقّم 1..maxusers مستقلًا عن عدّاد
    `cccam.clientid`، فيصطدم معرّف خانة بمعرّف عميل CCcam والتريس
    يطابق بالمعرّف وحده — صارت الخانات تسحب من عدّاد CCcam نفسه
    (التسوية اللاحقة تمنح العملاء العاديين ما بعده، فيبقى التفرّد
    داخل أي بناء إعداد). والاختبار صار يزرع عميل CCcam عاديًا
    (`F:`) بجانب الخانات ويشترط تفرّد كل المعرّفات في قائمة
    الصفحة. وأول تشغيل TSan بعدها أظهر سباقًا كامنًا أوسع:
    لقطة الصفحة كانت تقرأ flags/الحالة/lastecm عارية بينما عامل
    الاتصال يكتبها — أُصلح بأخذ اللقطة تحت `client_state_lock`
    (قفل الكاتبين أنفسهم) لقائمتي CCcam وFreeCCcam، وتغليف
    كتابات إعداد اتصال FreeCCcam بنفس القفل. النتائج: TSan
    ‏7/7 صفر، وحدات 1142/1142، `MEMCHECK: clean`، وci-validate
    أخضر على النكهتين. **يبقى بالاسم**: عند إعادة التحميل تعود
    العدّادات من 1 في الإعداد الجديد والعملاء المحفوظة تبقي
    معرّفاتها؛ التفرّد مضمون داخل البناء الواحد، وتدقيق إعادة
    التحميل المتقاطع للأجيال عمل أوسع.

29. **تفرّد المعرّفات عبر أجيال إعادة التحميل (2026-11-14)**: دين
    بند 28 المتبقي أثبت عطلًا حقيقيًا لا مجرد خطر: عند reload يغيّر
    إعدادات FreeCCcam (MAXUSERS ‏5→7 مثلًا) تُستبدل قائمة الخانات
    بخانات مرقّمة من عدّاد الإعداد الجديد (1..7)، بينما عميل CCcam
    المحفوظ حيًا يُبقي معرّفه (6) — تصادم فعلي داخل القائمتين
    الحيتين، التقطه اختبار جديد قبل الإصلاح حرفيًا
    ‏(`[7,6,5,4,3,2,1,6]`). الإصلاح: عند التبديل تُعاد ترقيم
    الخانات الواردة من عدّاد الجيل الحي (وهو متقدّم على كل معرّف
    حي)، وبعد التحديث يُرفع عدّاد الحي إلى ما بلغه عدّاد الجديد
    إن تجاوزه فلا يتراجع خلف معرّفات دخلت مع الجيل. الاختبار
    `tests/client-id-reload.py` (هدف في tests/Makefile) أحمر على
    الكود القديم وأخضر على الجديد، ويشترط أيضًا بقاء معرّف
    العميل المحفوظ كما هو. جلسة TSan ‏7/7 صفر سباقات (سيناريو
    reload داخلها)، والوحدات 1142/1142، و`MEMCHECK: clean`.
    بهذا يُغلق دين المعرّفات بطبقتيه (بناء واحد + عبر الأجيال)
    لدائرة cc_client_data التي يطابقها التريس؛ عدّادات أنواع
    العملاء الأخرى (newcamd/camd35/...) فضاءات منفصلة بنيويًا ولا
    يطابقها التريس أصلًا.

30. **حزمة التفعيل الميداني (2026-11-14)**: بعد أن صارت كل الديون
    التقنية القابلة للتنفيذ مغلقة، جُهّزت خطوة التفعيل التي بقيت
    قرار مشغّل: مقتطف `field-activation-example.cfg` في الجذر
    (‏`CACHE PROTECTION: STANDARD` + `CACHE CI VALIDATE: ON`،
    بمرحلتي رصد ثم قرار وتراجع موثق)، وقسم جديد في
    `docs/FIELD-GUIDE-ar.md` يربطه بصفحة `/user-trace` كمصدر
    ملاحظة وحيد. قُبل المقتطف حيًا من المفسّر نفسه (سطرا التسليح
    في سجل الإقلاع والصفحة 200). لا تغيير كود في هذه الجولة؛
    القرار الميداني وقرار حجب DoubleCheck وقرار EMM وفتح PR
    تبقى للمشغّل كما هي مسماة منذ بنودها.

31. **تدقيق ثانٍ بجرد آلي شامل (2026-11-14)**: فُحصت 118 رأسًا
    مبرمجيًا (استخراج دوال وعدّ مستدعين فعليين، مع تحقق يدوي من
    منطقة `#ifndef THREAD_DCW`) وقورنت بالطرق الخارجية المحدّثة
    (OSCam: ‏cwcycle، ‏cw_check بوضعيه، dropdiffs، nopushafter،
    wait_time المتدرج، localgenerated_only). الجديد المثبت حيًّا
    ولم يُحصَ في بند 25: الدوران الفوري في loadbalance، بوابة
    trustagg عند مروحة الكاش، آلية الدورة داخل
    cache_after_consensus، وcwreuse المُدان. والمؤكد ميتًا
    بالعدّ: جلّ عائلة رؤوس R22 (كتابة بلا قارئ)، ومراحل
    autorecov بلا مستهلِك توجيه، وسطر إقلاع كاذب آخر
    ‏(`CACHEX-ADVANCED ... active`) صُحّح في نفس الجولة. الفجوات
    مرتبة: حسم الخدمة عند الشك (mode 2)، نقل dropdiffs حيًّا خلف
    مفتاح OFF، وNoPushAfter صريح. التفصيل في
    `docs/AUDIT-BLACKSCREEN-CACHE-ar.md` (الإصدار الثاني).

32. **DropDiffs حي خلف `CACHE DROPDIFFS: ON` (2026-11-14)**: تنفيذ
    توصية تدقيق بند 31 الأولى القابلة للتنفيذ. النقل كشف أن تنفيذ
    الكود الميت كان سيبقى أعمى عن أهم سيناريو: الرد الخطّي المتأخر
    لا يُمرَّر لعامل Set-DCW أصلًا إن كان أول نجاح من خادم آخر
    (يُسقَط عند `different dcw` في كل بروتوكول بطاقة) — فصار عند
    التسليح فقط يُمرَّر للتحقق في البروتوكولات الخمسة
    (newcamd/cccam/camd35/cs378x/radegast)، وOFF = إسقاط stock
    حرفيًا. البوابة في ecm_setdcwdata: مرجع «معروف جيد» يُخزَّن
    عند التزام كل CW بهوية `ecmd5` الكاملة (لا الهوية المفبركة
    من 5 بايتات في النسخة الميتة) مع TTL احتياطي 60 ثانية
    أضيف لجدول المراجع، ومختلفٌ لاحق لنفس ECM يُرفض وحدثه على
    `/user-trace`. حدوده مصرّحة في كوده واختباره: أول واصل لا
    يُحاكَم هنا أبدًا (ذلك شغل الإجماع والبروفايل). الاختبار
    `tests/dropdiffs-live.py`: بطاقتان لنفس القناة، السريعة
    تُسلِّم وتُخزَّن، والبطيئة المختلفة تُرفض مع ON ولا تُرفض
    مع OFF، والعميل يستلم مفتاح السريعة في الحالين — أخضر على
    stock وr22. أُضيف السطر لحزمة التفعيل الميداني. جلسة TSan
    ‏7/7 صفر، وحدات 1142/1142، `MEMCHECK: clean`، وشبكة الكاش
    17/17. **تبقى فجوة بند 31 الأولى**: حسم الخدمة عند الشك
    (mode 2) على مسار الرد، وNoPushAfter الصريح — بتصميم مستقل
    لا بوابة مستعجلة.

33. **التخصيص لكل CAID للبوابات الجديدة (2026-11-14)**: كانت
    بوابتا CI وDropDiffs عامّتين فقط، فصارتا ثلاثيتي الحالة لكل
    بروفايل (`CI VALIDATE: ON|OFF` و`DROPDIFFS: ON|OFF` داخل قسم
    البروفايل؛ غياب السطر = وراثة العام، ونفس نمط dcwfilter
    الثلاثي). اختبارا البوابتين صارا يثبتان الاتجاهين معًا: عام
    ON + بروفايل OFF يُسلّم (التجاوز يفوز)، وبروفايل ON بلا أي
    سطر عام يرفض. وفي ملف البروفايلات الجاهز
    `multics-r24-optimized.cfg` سُلّحت العائلات المعرّضة
    للفايك الموجّه (Nagra: ‏1810/1811/1814/1883/1884 — نفس قائمة
    بوابة الشكل الموجّهة منذ R48) بالسطرين داخل أقسامها، وبقيت
    بروفايلات المفاتيح الثابتة (BISS ‏2600) بلا تسليح لأن شكل
    مفتاحها الطبيعي (نصفان متماثلان) هو ما تصطاده هذه البوابات.
    حراسة الملف: cfgcheck أخضر وprof ‏(إقلاع 72/72) أخضر.
    جلسة TSan ‏7/7 صفر، وحدات 1142/1142، `MEMCHECK: clean`،
    والبوابتان خضراوان على النكهتين بتسليحهما البروفايلي.
    **يبقى اختياريًا للمشغّل**: تسليح عائلات أخرى (0500/0100
    كثيرة البروفايلات) بعد رصد ميداني، لا تخمينًا من عندنا.

34. **الملف المتكامل 30W–52E (2026-11-14)**: أنشئ
    `multics-full-30w-52e.cfg` = الملف الأساسي (72 بروفايل)
    + 19 بروفايل موثّقًا من جداول OSCam srvid/CAID العامة
    (Sky Italia 09CD، Polsat 1861، Canal+ PL 0B01، Kabelio
    4AFC، Antik 0B00، Sky DE 09C4/098C، HD+ 186A، ORF
    0648/0650، Skylink 0624، CS Link 0666، Sky UK 0961،
    Canal Digital Nordic 0B00، Focus Sat 0B02، Digiturk
    0604، Tricolor 4AE1، NTV+ 0500، Viasat Baltic 093E)
    بمنافذ فريدة 1982-2000 وقوالب عائلات مطابقة لقواعد
    casystem (iCam بلا نصف-مفتاح، CSA 09xx بنصف+تبديل،
    Nagra الجديدة مسلّحة CI/DropDiffs كبند 33). عولج فخ
    0961 (VideoGuard CSA لا iCam). ‏cfgcheck صار يعرف العددين
    (72/91) ويفحص الملفات الثلاثة، وهدف `prof-full` جديد
    يثبت إقلاع الملف الكامل واستماع منافذه الـ91 كلها.
    **حدود مسماة**: لا بروفايل لـPowerVu ‏(7E/9E) ولا باقة
    بطاقات موثّقة على 52E (FTA/BISS يغطيه 2600)؛
    `PROVIDERS: 000000` حيث لا قائمة موثّقة، ولا SID LIST
    ولا خطوط — بيانات مشغّل يضيفها بنفسه.

35. **توحيد الملفات على 91 بروفايل ودفعها (2026-11-14)**:
    بطلب المشغّل عُدّلت الملفات نفسها لا ملف منفصل فقط:
    أُضيفت بروفايلات بند 34 التسعة عشر إلى
    `multics-r24-optimized.cfg` مباشرة، وأُعيد توليد
    `multics-r24-expert-reviewed.cfg` بمولّده الرسمي
    (`tools/generate-expert-config.py`، حُدّث تأكيده إلى 91)،
    وبقي `multics-full-30w-52e.cfg` نسخة موسّعة التوثيق بنفس
    الـ91. ‏cfgcheck صار يتوقع 91 في الملفات الثلاثة.
    التحقق: cfgcheck ثلاثتها خضراء، و`prof` (الملف الأساسي
    بـ91 منفذًا) و`prof-full` خضراوان. ملاحظة: اختبار
    `tests/expert-config.py` غير المربوط في أي هدف ما زال
    يؤكد افتراضًا قديمًا (CACHE EVIDENCE مطفأة في optimized)
    يتعارض مع تفعيلها المتعمد الموثّق — دين تسمية قائم، لم
    يُغيَّر سلوك الملف من أجله.

36. **النسخة الأساسية هي `bin/multics.x64` (2026-11-14)**:
    أكّد المشغّل أن `bin/multics.x64` (نكهة stats، اسم التسليم
    الثابت منذ R34/D85) هي النسخة الأساسية للعمل، فأُعيد بناؤها
    من الكود الحالي (`make release-stats`) وتحقق منها هي
    تحديدًا: `prof-full` بالملف المتكامل 91 بروفايل أخضر،
    وبوابتا CI VALIDATE وDropDiffs خضراوان عليها، وsmoke
    (HTTP/threads/json/smartfilter/telemetry) أخضر. حُدّث
    `bin/README-R24-x64.md` ببصمتها الجديدة. من الآن أي تحقق
    حي يُذكر ثنائيُّه صراحة والافتراضي هو هذا الملف.

37. **الشفرة الصحيحة من الكاش لقناة 1884:1136 (2026-11-14)**:
    أفاد المشغّل من الميدان أن القناة تأتي من الكاش في نسخة
    أخرى وتعمل بامتياز، وهدفه الشفرة لا اسم المرسل. أظهر تتبّعه
    رفض CW الكاش بـchecksum على `1884:000000:1136`، فأعاد
    اختبار `tests/cache-1884-compare.py` إنتاج الحالة محليًا
    وكشف خللًا حقيقيًا: استثناء `CW_CHECKSUM_EXEMPT` الدقيق
    كان يُحترم في تحقق البروفايل (الكاش والتسليم) لكن بوابة
    FAKE-CW الاحتياطية لناغرا في `ecm_setdcwdata` كانت تعيد
    فحص checksum متجاهلةً الاستثناء فترفض الشفرة الصحيحة.
    أُصلح بإضافة `dcw_checksum_exemption_applies()` واستعماله
    في البوابة الاحتياطية — الاستثناء يبقى دقيقًا (CAID+
    PROVID+SID، لا wildcard، ولا يعطّل null/repeat/BAD-DCW/
    pattern). الاختبار (6 حالات: مفلتر/غير مفلتر/مستثنى/
    نطاق خاطئ) كان أحمر في حالة الاستثناء وصار أخضر كله،
    ورُبط هدفًا دائمًا `c1884` في `tests/Makefile`. وسُلّح
    بروفايل 1884 في الملفات المسلّمة بسطر
    `CW_CHECKSUM_EXEMPT: 000000:1136` (بيانات المشغّل من
    تتبّعه لا اختراع). أُعيد بناء `bin/multics.x64`
    (release-stats) والاختبار أخضر عليها؛ وحدات 1142/1142،
    `MEMCHECK: clean`، TSan ‏7/7 صفر، cfgcheck ×3 وprof-full
    أخضران. **يبقى**: تأكيد الصورة فعليًا على جهاز المشغّل
    بعد التحديث — الاختبار يثبت التسليم لا الصورة.

38. **CWPAIR — اقتران شفرات SimulCrypt (2026-11-14)**: بطلب
    المشغّل بُحثت الفكرة خارجيًا (DVB SimulCrypt ‏ETSI TS 101
    197: شفرة واحدة لكل الأنظمة المتوازية) وبُرمجت في
    `src/cwpair.h`: اكتشاف (`CWDEDUP_CHECK` + قائمة CAID)
    يتعلم الأزواج من كل CW ملتزمة أو مخزنة من الكاش، وتقديم
    لكل بروفايل (`CWPAIR CAID/SID/ACTIVE/MAXAGE/SKIPCYCLE/
    STRICTCYCLE`) عند الفقد الكلي قبل إعلان الفشل. أول تنفيذ
    قدّم عبر طابور setdcw فخسر سباق مهلة ECM (التقديم ظهر في
    السجل والعميل لم يستلم)، فصار الالتزام متزامنًا بعد فحص
    بروفايل القناة المحلية. عدّادات وقائمة أزواج جاهزة للنسخ
    في `/user-trace` عبر field-status. اختبار
    `tests/cwpair-live.py` (توأم 183D من الكاش يُقدَّم لقناة
    183E بلا أي مصدر؛ ووضع القياس لا يقدّم) أخضر 3/3 على
    `bin/multics.x64` وهدف `cwpair` مربوط دائمًا. الملفات
    المسلّمة تحمل أسطر الاكتشاف فقط (تعلم بلا تقديم)؛ خرائط
    SID لا تُخترع — يلصقها المشغّل من اكتشافه. وحدات
    1142/1142، `MEMCHECK: clean`، TSan ‏7/7 صفر، prof-full
    وsmoke أخضران. **يبقى ميداني**: تأكيد صورة 1814↔1802
    بمصدر سريع عند المشغّل (مصدره الحالي أبطأ من عمر الشفرة
    كما أفاد)، وتفعيل ACTIVE لكل بروفايل قراره بعد عدّادات
    القياس.

39. **تخصيص CWPAIR لكل CAID في الملفات المسلّمة (2026-11-14)**:
    بطلب المشغّل كُتبت كتل CWPAIR داخل بروفايلات التوائم
    الموثّقة نفسها في الملفات الثلاثة (وأُعيد توليد نسخة
    الخبير): TivuSat ‏183D↔183E وHD+ ‏1830↔1843 وMEO/NOS
    ‏1814↔1802 — كلها `CWPAIR ACTIVE: NO` (قياس أولًا) و
    `MAXAGE: 21000`. صُحح خطأ أول تمرير: بروفايل DIGI TV
    ‏1802 (رومانيا) كاد يرث توأم NOS البرتغالي لمجرد تشابه
    CAID فأُزيل — التخصص بالباقة لا برقم CAID وحده. بلا أسطر
    `CWPAIR SID` عمدًا: خرائط القنوات من صفحة اكتشاف المشغّل
    لا تُخترع، والكتلة بدونها خاملة وموثّقة. ‏cfgcheck ×3
    وprof-full (91 منفذًا مع الأسطر الجديدة) أخضران.

40. **جولة التأكيد الشاملة (2026-11-14)**: بطلب المشغّل
    أُعيدت الجولة كاملة على الثنائي الأساسي `bin/multics.x64`
    (أُعيد بناؤه release-stats ولم تتغير بصمته — بناء قابل
    لإعادة الإنتاج من الكود المدفوع): وحدات TEST=0،
    `MEMCHECK: clean`، TSan-R22 نظيفة (0 تقرير)، cfgcheck
    للملفات الثلاثة، prof + prof-full (91/91 منفذًا)، c1884
    ‏(6/6)، cwpair (تقديم + قياس)، CI VALIDATE وDropDiffs على
    نكهتي stats وr22، smoke، DoubleCheck حي، FreeCCcam حي،
    وتفرّد معرّفات reload — الكل أخضر بلا استثناء. حُدّثت
    بصمة الثنائي في `bin/README-R24-x64.md`. لا تغيير سلوك في
    هذه الجولة: تأكيد فقط.

41. **CACHEEX NOPUSH AFTER بحسب CAID (2026-11-14)**: نُقلت
    فكرة OSCam من رأس `cacheex-advanced.h` الميت إلى مسار
    الدفع الحي في `src/nopushafter.h`: سطر
    `CACHEEX NOPUSH AFTER: <CAID>:<ms>,...` (CAID دقيق فقط،
    لا أقنعة عائلات ولا افتراضات) يجعل CW المتأخرة عن عمر
    CAIDها تُسلَّم للعميل المنتظر ولا تُدفع لـCacheEX/CSP.
    أثناء التنفيذ ظُبط فرع مفسّر مظلَّل بفرع CACHEEX أقدم
    (نفس درس بند 33) فنُقل التحليل إليه. اختبار
    `tests/nopushafter-live.py` ببطاقة بطيئة (1200ms) ونظير
    مراقب: بلا السطر تُدفع الشفرة، ومع `1884:500` يستلم
    العميل ولا يصل النظير شيء — أخضر على `bin/multics.x64`
    وهدف `nopush` مربوط. عدّاد الكبح في `/user-trace`.
    وحدات/ميمتشك/TSan ‏7/7/cfgcheck/prof-full خضراء. بذلك
    تُغلق فجوة بند 31 الثانية؛ تبقى فجوة «حسم الخدمة عند
    الشك mode 2» وحدها وتحتاج تصميمها المستقل.

42. **حسم الخدمة عند الشك CACHE SERVICE DOUBT (2026-11-15)**:
    آخر فجوات بند 31 المسماة (OSCam ‏cacheex_cw_check mode 2).
    سطر عام `CACHE SERVICE DOUBT: ON` داخل فرع CACHE الفائز في
    المفسّر (الدرس المتكرر: الفروع المكررة تظلّل). داخل منتقي
    ‏CACHE EVIDENCE الاختياري `cache_find_select_r48()`: عند
    وجود شفرتين متميّزتين مؤهلتين على الأقل لمدخل واحد وبلا
    أغلبية نظراء صارمة لواحدة (top <= second) يُرفض خدم الكاش
    ويسقط ECM لمساره العادي؛ الأغلبية الصارمة تُخدَم. الرفض
    مقصور على مسار FIND ولا يمس وصول الشفرات أثناء الانتظار.
    اختبار `tests/service-doubt-live.py` (هدف `svcdoubt`):
    دافع CacheEX ‏(cxpeer) يدفع شفرتين صحيحتي checksum لقناة
    ‏1884 ثم ينقطع؛ تعادل 5/5 لا يُخدَم، أغلبية 6/5 تُخدَم،
    وبالبوابة OFF يُخدَم التعادل — الأوضاع الثلاثة خضراء على
    ‏`bin/multics.x64` مع عدّادات الصفحة شاهدة. تذبذب أولي في
    الاختبار (فقد دفعة عند الازدحام) ثُبّت بالمباعدة بين
    الدفعات لا بإضعاف أي بوابة. وحدات وميمتشك وTSan وcfgcheck
    وprof-full (91/91) خضراء. بذلك تُغلق فجوتا بند 31
    المسماتان معًا؛ التفعيل الميداني قرار مشغّل وعدّادات
    ‏`/user-trace` تخبر قبل المخاطرة.

43. **SERVICE DOUBT بروفايليًا (2026-11-15)**: بند 42 كان
    عامًا فقط لأن المفسّر الأصلي لا يعرف توجيهًا بروفايليًا له؛
    أُضيف `SERVICE DOUBT: ON|OFF` داخل البروفايل بثلاثية بند 33
    نفسها (0=يرث العام، 1=يفرض ON، 2=يفرض OFF) فيتقدّم على
    ‏`CACHE SERVICE DOUBT` العام بالاتجاهين، ويظهر عدّ
    البروفايلات المتجاوزة في `/user-trace` (أثناء ذلك ضُبط
    خلل عرض: فقرة التجاوز كانت تكتب فوق سطر العدّادات قبل
    إرساله فأُصلح الترتيب). وُسّع `tests/service-doubt-live.py`
    بوضعي prof-on/prof-off فصارت الأوضاع خمسة خضراء على
    ‏`bin/multics.x64`، وهدف `svcdoubt` يغطيها كلها. وحدات
    وميمتشك وTSan وcfgcheck وprof-full (91/91) خضراء.
    الأسطر لم تُضف للملفات المسلّمة: التفعيل لكل باقة قرار
    مشغّل بقيمه.

44. **تدقيق قارئي cli->handle الخام — الإغلاق الجزئي المثبت
    (2026-11-15)**: دين مسمّى منذ بند 24. الجرد الكامل (193
    موضعًا) صُنّف يدويًا: قراءات خيط المالك لبروتوكوله (newcamd/
    cccam/camd35/cs378x/mgcamd لعميله نفسه) ومسارات إغلاق
    config.c بعقود أقفالها الموثّقة ولقطة httpserver تحت قفل
    حالة العميل — كلها بقيت كما هي عمدًا لأنها إما أحادية الخيط
    أو محكومة بقفل. أُصلح ما ثبت أنه قارئ عابر للخيوط بلا قفل:
    (أ) حلقة بناء مصفوفة poll في `frcc_recvmsg_thread`
    (srv-freecccam.c) كانت تقرأ `cli->handle` خامًا بينما جانب
    الإرسال في الدالة نفسها يحمّل ذرّيًا منذ بند 22 — النصف
    المفقود من النشر الذرّي، وصار التحميل واحدًا موحّدًا؛
    (ب) تخزين `frcc_disconnect_cli` صار ذرّيًا ليقرن القارئين؛
    (ج) قراءات العرض في httpserver (fd عميل CCcam، عدّاد وعرض
    Radegast) وtelnet (عميل mgcamd) وقراءة خيط دفع CacheEX
    لمقبض عميل cs378x — كلها تحميلات ذرّية RELAXED بلا أي
    تغيير سلوك أو بروتوكول. خيط الكاش لمقبضه الخاص وخوادم
    الاستماع في srv-common خارج الفئة (مالك واحد). إثبات:
    freecccam-live وci-validate-live ووحدات وميمتشك وTSan
    نظيفة وcfgcheck وprof-full (91/91) على `bin/multics.x64`.
    الباقي بالاسم: قراءات خيط المالك الخام داخل ملفات
    البروتوكولات تبقى دينًا موثّقًا لا سباقًا مثبتًا — TSan
    نظيفة عليها ولم تُلمس بلا اختبار يثبت العطل.

45. **ربط خطافي cw_failed/cw_verified بنقطتي دليل نزيهتين
    (2026-11-15)**: كانا يُستدعيان من لا مكان وتُحصى إحصاءاتهما
    أصفارًا كاذبة في نكهة r22. التعليق القديم في setdcw.c كان
    محقًا في رفض استدعائهما عند التسليم (لا رسالة بروتوكولية
    تقول إن عميلًا فكّ التشفير فعلًا — الاستدعاء هناك اختلاق
    دليل). الربط النزيه الوحيد: ‏cw_failed عند نقطة الإيداع
    القاطع في الذاكرة السلبية `cwn_note` (برهان GR3: نزاع دفتر
    + فشل عميل، أو برهان إعادة استخدام — مرّة واحدة لكل مفتاح
    جديد وبعد تحرير قفل cwneg)، وcw_verified عند حكم اتفاق
    ‏DoubleCheck ‏(v==1: مصدران مستقلان أنتجا بايتات متطابقة —
    توثيق لا إثبات فكّ تشفير، ومسمّى كذلك في التعليق). في stock
    كلاهما no-op؛ سلوك التسليم لم يتغيّر في أي نكهة. أخضر:
    doublecheck-live على stock وr22، وحدات، ميمتشك، TSan
    نظيفة، cfgcheck وprof-full ‏91/91.

46. **إكمال `/user-trace` كسطح تشخيص ميداني وحيد (2026-11-15)**:
    بطلب المشغّل أن تكون الصفحة كل ما يلزم لتحليل المشاكل
    وحلّها دون سجلات طرفية. أُضيف سطر «البوابات المسلّحة»:
    ‏CI VALIDATE وDROPDIFFS (الحالة العامة + عدد البروفايلات
    الفارضة ON/OFF)، عدد بروفايلات CACHE EVIDENCE، وعدد
    بروفايلات استثناء checksum (بند 37) — تُملأ في
    ‏mcs_field_status_fill من بنية الإعداد الحية. اختبار
    ‏svcdoubt صار يتحقق من وجود السطر وصحة عدّ CACHE EVIDENCE
    فيه على الصفحة نفسها.

47. **autorecov: من ادعاء استرداد إلى مراقبة صادقة (2026-11-15)**:
    التدقيق وجد: `autorecov_tick` (قارئ المراحل الوحيد) بلا أي
    مستدعٍ، لا كود يبني قرارًا على أي مرحلة، العدّادات لا تُقرأ
    في أي مكان، التفعيل مثبّت `1` عند الإقلاع خلافًا لتوثيق
    «default OFF»، لا توجيه إعداد أصلًا، والجدول يُكتب من خيوط
    خوادم CCcam متعددة ويُقرأ (الآن) من خيط HTTP بلا أي قفل.
    الإصلاح دون اختلاق سلوك استرداد: توجيه `AUTO RECOVER:
    ON|OFF` (افتراضي OFF) يُقرأ حيًا عند كل استدعاء فيتبع
    إعادة التحميل، قفل ورقة داخل الجدول لكل الدوال العامة،
    وسطر مراقبة في `/user-trace` (بدأ استرداد/نجاحات/قنوات في
    مرحلة) يصرّح أنه مراقبة فقط. المشغّلات والنجاحات من
    ‏cli-cccam كما هي؛ لم يُخترع أي فعل استردادي. البوابات
    خضراء على النكهتين.

48. **تدقيق حقول cfg خارج بوابة الإعدادات — بلا تغيير (2026-11-15)**:
    جُردت القراءات: النمط العام في الكود (ومنذ r82) قراءة قياسات
    ‏cfg.cache.* مباشرة من خيوط الكاش/ECM دون بوابة القراءة،
    بينما الكتابة عند إعادة التحميل محكومة بعقود الأقفال
    الموثّقة للخوادم/العملاء والبنية. القراءات كلمات مفردة
    (word-sized) لا تُبنى عليها قرارات بنيوية، وجلسة TSan نظيفة،
    ولا اختبار يثبت عطلًا. القاعدة المثبتة في هذا المشروع:
    لا تغيير دون إثبات عطل. الحكم: دين موثّق بالاسم، يُعاد فتحه
    فقط إذا أنتجت جلسة TSan موسّعة عند إعادة التحميل تقريرًا —
    لا إصلاح تخمينيًا شاملًا.

49. **إكمال تخصص Nagra في الكونفيغات المسلّمة (2026-11-15)**:
    بطلب المشغّل «الإعدادات بالتخصص لكل CAID». كان تسليح
    ‏CI VALIDATE/DROPDIFFS مقصورًا على 7 بروفايلات Nagra
    (1810/1811/1814/1883/1884 من بند 33 و1861/186A من بند 34)
    بينما عائلة 18xx في الملفات الثلاثة 25 بروفايلًا. أُضيف
    ‏`CI VALIDATE: ON` و`DROPDIFFS: ON` للثمانية عشر الباقية
    (1802 بقناتيه، 1803، 1805، 1813، 1815، 1817 بقناتيه، 1818،
    1819، 1830، 183D، 183E، 1840، 1841، 1860، 1880) بعد سطر
    ‏CAID مباشرة، في optimized وfull-30w-52e، وأُعيد توليد
    ‏expert-reviewed بأداته — 36 سطرًا مضافة لكل ملف، بلا أي
    حذف أو تغيير قيم قائمة. البوابتان مثبتتان حيًا منذ بندي
    ‏26/32/33 ولكل بروفايل مخرج `OFF` بروفايلي عند الحاجة.
    ‏cfgcheck وprof-full (91/91) أخضران. ما لم يُمسّ عمدًا:
    ‏0500 (Viaccess) و0100 (Seca) — لا دليل فايك موثّق في
    المشروع لهما كمجموعة Nagra، وتسليح بوابات رافضة عليهما
    يحتاج رصدًا ميدانيًا أولًا؛ وCACHE EVIDENCE/SERVICE DOUBT
    لم تُسلّح في أي بروفايل (تغيّر مسار خدمة يحتاج مراقبة
    عدّادات الصفحة أولًا كما هو موثّق).

50. **تأسيس العام ونقل الخاص إلى دليل التريس (2026-11-15)**:
    بصياغة المشغّل: «نأسس العام ونمر للخاص إن وجدت مشاكل من
    التريس». صارت القاعدة مكتوبة في الدليل الميداني وفي
    الكونفيغات نفسها: أُضيف للملفات الثلاثة (expert مولّد)
    بلوك عام صريح بعد `CACHE EVIDENCE: ON` يوثّق افتراضيات
    البوابات الرافضة OFF بقيم stock نفسها (`CACHE CI
    VALIDATE: OFF`، `CACHE DROPDIFFS: OFF`، `CACHE SERVICE
    DOUBT: OFF`، `AUTO RECOVER: OFF`)، فلا يخمّن قارئ الكونفيغ
    الأساس، وأي تسليح خاص لاحق يُقرأ استثناءً مقصودًا له.
    تصحيح صدقي لبند 49: قوله إن CACHE EVIDENCE «لم تُسلّح في
    أي بروفايل» غير دقيق للملفات المسلّمة — السطر العام
    ‏`CACHE EVIDENCE: ON` يجعلها فعّالة بالوراثة في كل
    البروفايلات (وعداد الصفحة يحسب الفعّال لا المكتوب)،
    والصحيح أن SERVICE DOUBT وحدها غير مسلّحة في أي مكان.
    ‏cfgcheck وprof-full (91/91) أخضران على الأسطر الجديدة.

51. **شرط إعادة فتح بند 48 صار اختبارًا دائمًا (2026-11-15)**:
    بند 48 أبقى قياسات cfg.cache.* خارج البوابة دينًا موثّقًا
    بشرط: يُعاد فتحه فقط إن أنتجت جلسة TSan موسّعة عند إعادة
    التحميل تقريرًا. بُنيت الجلسة: سيناريو `gates` جديد في
    ‏tests/tsan-r22-session.sh (داخل هدف tsan-r22 الدائم) —
    ‏tests/tsan-gates-reload.py يبقي مدخل كاش 1884 حيًا بدفع
    ‏CacheEX وعميل يسأل بلا توقف (224 سؤالًا في التشغيل
    المرجعي)، بينما خيط إعادة تحميل يقلب في نفس الـinode وبكتابات
    متساوية الطول: ‏CACHE SERVICE DOUBT وCACHE CI VALIDATE
    وAUTO RECOVER وCACHE THRESHOLD كل ~0.7s. النتيجة على ثنائي
    ‏TSan+R22: صفر تقارير سباق، والخادم حي يجيب HTTP في
    النهاية؛ والجلسة الكاملة بسيناريواتها الثمانية نظيفة.
    الحكم على دين بند 48: الشرط اختُبر ولم يتحقق — يبقى
    موثّقًا بالاسم، مدعومًا الآن بدليل سلبي حي لا بانطباع،
    ولا إصلاح تخمينيًا.

52. **تدقيق التخصص لكل CAID ضد مواصفة العائلات (2026-11-15)**:
    بطلب المشغّل المتكرر «الكونفيغ والإعدادات بالتخصص لكل
    ‏CAID». دُقّقت الملفات الثلاثة عائلةً عائلة ضد مواصفة
    ‏R40 المكتوبة في رأس الكونفيغ نفسه وضد سياسة cfgcheck
    الحاكمة: Nagra ‏18xx ‏(HALFNULLED: NO + بوابات بند 49)،
    ‏Conax/CryptoWorks/Irdeto/Seca/Viaccess/BetaCrypt/DRE
    ‏(HALFNULLED: NO، لا SWAP)، BISS/Tandberg مفاتيح محلية،
    وVideoGuard: ‏CSA نصف+SWAP والإستثناء iCam. المحاولة
    الأولى عاملت 09CD/09C4/098C كـCSA ناقصة فأوقفها cfgcheck
    فورًا: ثلاثتها iCam AES (قائمته الحاكمة ستة: 0963/098C/
    098D/098E/09C4/09CD) وكانت مطابقة أصلًا — تُراجع عن
    التغيير كليًا وسُجّل أن الحارس يعمل كما يجب. الإصلاح
    الوحيد الباقي صدقي: تعليق R40 في رأسي optimized وfull
    كان يسمّي ثلاثة iCam فقط، فصار يسمّي الستة (وexpert
    أُعيد توليده). cfgcheck وprof-full (91/91) أخضران.
    الخلاصة للمشغّل: التخصص لكل CAID قائم ومحروس آليًا؛
    أي انحراف مستقبلي عن سياسة عائلة سيرفضه cfgcheck قبل
    أن يصل الميدان، والتسليحات الإضافية (SERVICE DOUBT،
    ‏DoubleCheck قائمة CAID) تبقى بقاعدة بند 50: بإشارة
    من التريس على قناة بعينها.

53. **جولة تأكيد الإنتاج التجريبي (2026-11-15)**: بموافقة
    المشغّل «جاهز للإنتاج التجريبي». أُعيدت الجولة كاملة على
    ‏`bin/multics.x64` (بصمة `01818b0bf1c18ea5e3a4f5f58803fa49`
    مطابقة لـREADME، ولا تغيير مصدري منذ بند 47 — ما بعده
    كونفيغات/توثيق/اختبارات): وحدات، `MEMCHECK: clean`،
    ‏TSan-R22 بسيناريواتها الثمانية نظيفة (0 تقارير)،
    ‏cfgcheck ‏(5/5)، prof وprof-full ‏(91/91)، c1884،
    ‏cwpair (خدمة + قياس)، nopush بالوضعين، svcdoubt
    بالأوضاع الخمسة، DoubleCheck (تطابق/اختلاف على الصفحة)،
    ‏FreeCCcam حي، CI VALIDATE حي، وsmoke — الكل أخضر بلا
    استثناء. كُتبت قائمة الانطلاق `docs/PILOT-GOLIVE-ar.md`:
    ما يُنشر وبصمته، الأساس العام أول يوم، مراقبة الأسبوع
    الأول من `/user-trace` وحده، قاعدة التعديل الخاصة
    بالتريس، وخطوات الرجوع، والحدود المسماة (EMM، بطاقات
    حقيقية، Valgrind) بلا إخفاء.

54. **تحليل مرجع R200 v2.6 وتطوير المثبت فقط (2026-11-15)**:
    حُفظ الدليل المرفوع في `docs/reference-r200-v26.txt` وقورن
    بندًا-بندًا بكودنا. الخلاصة: أغلب منظومتهم موجود عندنا بأسماء
    أخرى وبصيغ أقوى — فلا نسخ أعمى:
    - **موجود عندنا**: ENTROPY ⇐ ‏`ssf_check_entropy` +
      ‏`cwentropy.h` ‏(CWEN_LOWDIV/NEAR باحتمالات محسوبة)؛
      ‏MULTICHECK ⇐ ‏`ssf_check_multicheck` + DoubleCheck الحي؛
      ‏DUPSEEN ⇐ ‏`ssf_check_crosschannel`؛ SF_CW_REUSE/SMART
      RETRY/SERVER SELECT ⇐ ‏`cw_reuse.h`/`smart-retry.h`/
      ‏`smart-server-select.h`. **لم يُتحقق من مقابل مباشر**
      عندنا لـCW1CYCLE ولا لحظر-IP-لكل-بروتوكول ولا لصفحة
      ‏`/loginlog` (عندنا عدّادات دخول لكل عميل في صفحات
      العملاء فقط) — سُجّلت فجواتٍ موثّقة لا «موجودات»،
      ولا تُبنى إلا بدليل تريس/حاجة تشغيلية (بند 50).
      رُفضا — إعادة استعمال نصف CW أو توليده تخمينًا يناقض
      قاعدة «الشفرة الصحيحة تصل العميل» وبواباتنا المثبتة حيًا،
      ولا دليل تريس يطلبهما. **صفحتا /iptables و/loginlog
      المستقلتان**: لم تُستنسخا — قاعدة المشروع أن `/user-trace`
      مصدر التشخيص الوحيد، وإضافة صفحات تشتت مصدر الحقيقة.
    - **عطلان مثبتان عندنا أُصلحا** (من قسم إصلاحاتهم):
      (أ) ‏`cs378x_send`/`camd35_send`/`camd35_sendto` كانت تنسخ
      ‏`len` إلى مكدّس 1024 بلا فحص (و`crc32(buf+20, len-20)`
      يقرأ خارجًا عند `len<20`) — حارس الآن: رفض `!buf ||
      len<20 || len>1000`. اختبار `tests/camd35-send-guard`
      ‏(ASan/UBSan) أخضر، وعلى الكود القديم يُسقط ASan بـ
      ‏stack-buffer-overflow — العطل والدليل معًا.
      (ب) بانيا ‏poll في `srv-cs378x.c` (موضعان) و`srv-camd35.c`
      كانا يكتبان `pfd[pfdcount++]` في مصفوفة `MAX_CSPORTS`
      ‏(200) بلا حدّ — تجاوز عند >199 عميلًا متصلًا؛ حارس الآن
      ‏`pfdcount < MAX_CSPORTS` وإلا `ipoll = -1` (على نمط
      ‏`srv-radegast.c` الصحيح عندنا أصلًا). ‏mlogf: عندنا آمن
      أصلًا (`vsnprintf` محدود، بند TASK 3.3).
    - **التحقق على الثنائي الأساسي** `bin/multics.x64` (بصمة
      جديدة `b1d2b1d5b45cc122162ad30a83422948`، حُدّثت في
      ‏README وPILOT-GOLIVE): بناء `release-stats` نظيف،
      ‏`camd35-guard` + ‏`cfgcheck` (5/5) + ‏smoke حي (HTTP
      ‏200 وكل الصفحات) خضراء. لا سلوك بروتوكولي تغيّر:
      الإطارات الطبيعية تمرّ كما قبل (يثبته الاختبار)، وما
      رُفض هو ما كان فيضًا/قراءة خارج الحدود فقط.
    - **ديون/مرفوضات مسماة**: MARCHCHECK عندهم غامض التعريف
      في الدليل (لا مواصفة قابلة للتنفيذ الأمين) — لم يُخترع
      له مقابل؛ إن وصلت مواصفته أو دليل تريس يطلبه يُبنى
      بقاعدة بند 50. تسليح أيٍّ من مكافئاتهم افتراضيًا عندنا
      يبقى OFF/قياسًا حتى دليل التريس كالعادة.

55. **MARCHCHECK: كاشف النصف المتوقف بنفس الخانة (2026-11-15)**:
    بعد بند 54 بقي MARCHCHECK دينًا لعدم وجود مواصفة؛ بإعادة
    قراءة المرجع (قسم 2.1) صارت المواصفة دقيقة فنُفّذت بأسلوبنا:
    على قناة «ماشية» شرعية ينتقل النصف المشترك للخانة الأخرى
    كل دورة ‎(A,B)→(B,C)‎؛ الفايك المعاد تدويره يُبقي نصفًا
    في **نفس الخانة** بينما يتغير النصف الآخر. التنفيذ فحص
    عاشر في `supersmartfilter_check` (تحت `ssf_lock` القائم)
    بجدول آخر-CW-مقبول لكل ‎(caid, provid, sid)‎ ‏(512 مدخلًا):
    - الأوضاع: عالمي `CACHE SF MARCHCHECK: 0|1|2` (0=OFF
      افتراضي stock، 1=تسجيل/عدّ فقط، 2=رفض بسبب 10)، وتجاوز
      بروفايلي بصيغتهم `DCW MARCHCHECK: 0|1|2` داخل البروفايل
      (يُخزَّن ‎+1‎ ليبقى 0=وراثة) يفوز بالاتجاهين.
    - إعفاءات أمينة: CW نصفية التصفير (أي نصف صفري) معفاة —
      صفرها متوقف بالتصميم؛ وCW مطابقة لسابقتها ليست خطأ
      مسير (مجال فحص التكرار)؛ والمرفوض بوضع 2 **لا** يصبح
      مرجع المقارنة التالي (المرفوض لم يُقبل أصلًا).
    - العدّادات `marchcheck_flagged`/`rejected_marchcheck`
      تظهر في صفحة SmartFilter (عمود + سطر «رُصد») — تحقق حي:
      الصفحة 200 والعمود ظاهر على `bin/multics.x64`.
    - الاختبار `tests/marchcheck` (ASan/UBSan على الكود
      الإنتاجي نفسه): مسير شرعي لا يُعلَّم، متوقف يُعلَّم
      بوضع التسجيل دون رفض، يُرفض بوضع 2 بسبب 10 ودون أن
      يصبح مرجعًا، إعفاء النصفي والمطابق، وتجاوز البروفايل
      يفوز على العام بالاتجاهين — أخضر كامل.
    - الكونفيغات: `CACHE SF MARCHCHECK: 0` صريح في الملفات
      الثلاثة (expert مولّد) مع تعليق قاعدة بند 50: التسليح
      يبدأ `DCW MARCHCHECK: 1` داخل بروفايل مضطرب واحد بدليل
      التريس، لا تعميمًا. لا CAID سُلّح فعليًا في هذا البند.
    - التحقق: بناء نظيف (بصمة `9c6a002f244045983d286b9c200263f7`)،
      ‏marchcheck + camd35-guard + cfgcheck ‏(5/5) + prof-full
      ‏(3/3) + smoke حي — الكل أخضر. درس صغير: البناء الإنتاجي
      بمعيار C أقدم — ممنوع إعلان متغير داخل `for` في الرؤوس.

56. **CW_DUP: كاشف الشفرة الثابتة عبر دورات ECM (2026-11-15)**:
    ثاني كاشفات مرجع R200 (قسم 2.5) بمواصفة دقيقة، نُفّذ بنمط
    بند 55 نفسه — فحص حادٍ عشر في `supersmartfilter_check` تحت
    ‏`ssf_lock`، بجدول 512 مدخلًا لكل ‎(caid, provid, sid)‎:
    - التعريف الأمين: تُعاد نفس الشفرة لدورات ECM **جديدة**
      ‏(ecm_hash مختلف) بينما القناة تتدحرج فعليًا — أي قُبلت
      شفرة مختلفة منافسة خلال آخر 120 ثانية. العدّاد `rep`
      يزيد فقط عند تغيّر الـhash؛ إعادة الإرسال ضمن نفس الدورة
      لا تُحسب أبدًا، ودحرجة مفتاحين طبيعية (A,B,A,B) تصفّر
      العدّ عند كل تبديل، ومفتاح ثابت بلا منافس **لا يُعلَّم
      أبدًا** (قد يكون شرعيًا) — وهذه حدود معلنة لا ثغرة خفية.
    - بلا `ecm_hash` لا يُثبت حدّ الدورة فلا يُحسب شيء
      ‏(fail-open موثّق). حالة المشاهدة تُحدَّث عند كل رؤية
      حتى عند رفض وضع 2، ليبقى الفايك العالق مرفوضًا بدل أن
      يصبح مرجعًا صامتًا (اختلاف مقصود عن بند 55 وموثّق في
      الكود: هناك المرجع نصف-شفرة، وهنا العدّ مشاهدة لا مرجع).
    - الأوضاع: عالمي `CACHE SF CWDUP: 0|1|2` (افتراضي 0)
      وعتبة `CACHE SF CWDUPREP: 2..10` (افتراضي 4)، وتجاوز
      ‏`DCW CWDUP: 0|1|2` بروفايليًا يفوز بالاتجاهين؛ الرفض
      بسبب 11. العدّادات في صفحة SmartFilter (عمود CW-Dup +
      ‏`cwdup_flagged`) — تحقق حي 200 على الثنائي الأساسي.
    - الاختبار `tests/cwdup` (ASan/UBSan): ثابت-بلا-منافس آمن،
      إعادة-نفس-الدورة آمنة، دحرجة آمنة، عالق-مع-منافس يُعلَّم
      عند REP بوضع 1 ويُرفض بسبب 11 بوضع 2 وشفرة شرعية بعده
      تمرّ، التجاوز بالاتجاهين، وغياب الـhash فشل-مفتوح —
      أخضر كامل، ومعه marchcheck من جديد بعد نموّ التوقيع.
    - الكونفيغات: `CACHE SF CWDUP: 0` + `CWDUPREP: 4` صريحة
      في الثلاثة مع قاعدة بند 50؛ لا CAID مسلّح فعليًا.
    - التحقق على `bin/multics.x64` (بصمة
      ‏`61e2debedd7fd745bc6793633694fd53`): cwdup + marchcheck +
      ‏camd35-guard + cfgcheck ‏(5/5) + prof-full ‏(3/3) +
      ‏smoke — الكل أخضر.

57. **حارس تخمين الويب: حظر IP بعد فشل متكرر (2026-11-15)**:
    فجوة موثقة من بند 54 (مقابل `HTTP LOGIN FAILS/BAN` عند
    ‏R200) أُغلقت بتوسيع وحدة `loginthrottle` القائمة (R3)
    لا بوحدة جديدة:
    - `login_note_failure_ex()`: العدّ وقرار الحظر ذرّيًا تحت
      نفس القفل؛ عند بلوغ العتبة يُختم `banned_until` ويُحصى
      الحظر. `login_banned()` بفحص آمن للالتفاف (int32)،
      و`login_ban_status()` للإجمالي/النشط. النجاح يصفّر
      العدّ **والحظر**، وانقضاء هدوء الخانة ينسى الاثنين —
      والعتبة 0 تُبقي سلوك R3 stock حرفيًا (الدالة القديمة
      ‏`login_note_failure` لم تتغير دلالتها).
    - الإعداد: `HTTP LOGIN FAILS: n` ‏(0=OFF افتراضي، ≤1000)
      و`HTTP LOGIN BAN: sec` ‏(افتراضي 3600 عند التسليح،
      ≤604800) في فرع HTTP LOGIN القائم. الإنفاذ في
      ‏`gererClient` قبل المصادقة بجانب باب قائمة السماح:
      إغلاق فوري + سطر `[LOGIN BAN]` واحد؛ ومشروط بقيمة
      الكونفيغ الحية — إعادة FAILS إلى 0 تُطفئ حتى الحظر
      القائم فورًا. عدّ الفشل مشترك بين البابين (جدول R3
      نفسه)، والإنفاذ للويب وحده كما يوحي اسم الإعداد؛
      التلنت يبقى بتأخيره التصاعدي فقط (توثيق لا ادعاء).
    - الرؤية: سطر «حارس تخمين الويب» في `/user-trace` (مصدر
      التشخيص الوحيد بقاعدة المشغّل): مسلّح/مطفأ، العتبة،
      المدة، إجمالي حظر الجلسة، المحظور الآن — تحقق حي.
    - الإثبات الحقيقي على `bin/multics.x64`: بكونفيغ
      ‏FAILS: 3 — ثلاث محاولات خاطئة ‎401‎، والرابعة **ببيانات
      صحيحة** أُغلقت قبل المصادقة (curl 000). وبلا الأسطر
      ‏(stock) ‏smoke أخضر كالعادة. اختبار الوحدة
      ‏`tests/loginban` بساعة مُدخلة (MCS_LT_NOGLOBALS):
      عتبة/انقضاء/إطفاء/نجاح-يمحو/نسيان-الهدوء — أخضر،
      واكتُشف أثناءه أن تشغيل اختبار موازيًا لتحرير ملفه
      يعطي فشلًا شبحيًا (درس تزامن أدوات، لا عطل كود).
    - الكونفيغات: `HTTP LOGIN FAILS: 0` + `BAN: 3600` صريحة
      في الثلاثة بتعليق التسليح المقترح (5/3600)؛ الافتراضي
      التشغيلي يبقى مطفأً حتى يقرر المشغّل — واجهة الويب
      خلف شبكته، والتسليح قراره بسطرين.
    - التحقق: بصمة `48ad74a36a551161b4fbf4edcdd03eed`؛
      ‏loginban + marchcheck + cwdup + camd35-guard +
      ‏cfgcheck ‏(5/5) + smoke حي — الكل أخضر.

58. **CW_REUSE: كاشف النصف المُعاد عبر القنوات (2026-11-15)**:
    ثالث كاشفات المرجع (قسم 2.6/V6.14): مولّدات الفايك تعيد
    تدوير بركة صغيرة من أنصاف 8-بايت عبر قنوات مختلفة. عندنا
    ‏`cw_reuse.h` ‏(TASK 1.3) يغطي الشفرة **الكاملة** عبر
    ‏ECMs مختلفة بنفس نطاق CAID/المزوّد كبرهان GR3 — وهذا
    الكاشف مختلف الدقة (نصف، وعبر CAID أيضًا) فبُني فحصًا
    ثاني عشر في `supersmartfilter_check` لا تكرارًا له:
    - حلقة 512 مشاهدة (شفرة كاملة + قناة + زمن). نصف غير
      صفري يظهر على ‎(caid, provid, sid)‎ مختلف داخل النافذة
      ضمن شفرة كاملة **مختلفة** = توقيع البركة المُعادة.
      في أي خانة كان النصف — المقارنة على الخانتين.
    - **استثناء التوأم معلن ومختبَر**: نفس الشفرة الكاملة على
      قناة أخرى (simulcrypt شرعي، مجال بند 38/TASK 1.3) لا
      يُعلَّم هنا أبدًا — بلا هذا الاستثناء لعاقب الكاشف
      ظاهرة بنيوية صحيحة. الأنصاف الصفرية لا تُتتبَّع،
      والمشاهدة المنتهية النافذة تُتبنّى بصمت، وإعادة رؤية
      نفس القناة تجدّد ولا تُعلّم.
    - الأوضاع بنمط البنود: عالمي `CACHE SF CWREUSE: 0|1|2`
      ‏(افتراضي 0) + `CWREUSEWINDOW` ‏(1000..600000، افتراضي
      ‏120000) + نطاق `CACHE SF REUSECAIDS` (فارغ=الكل)،
      وتجاوز `DCW CWREUSE` بروفايليًا؛ الرفض بسبب 12.
      العدّادات بعمود CW-Reuse في صفحة SmartFilter — حي 200.
    - الاختبار `tests/cwreuse` (ASan/UBSan): عبور قنوات
      يُعلَّم/يُرفض (12) وبخانة معكوسة، توأم آمن، انتهاء
      نافذة آمن، صفر آمن، نطاق CAID بالاتجاهين، تجاوز
      بروفايلي بالاتجاهين — أخضر، ومعه marchcheck/cwdup/
      ‏loginban/camd35-guard من جديد بعد نموّ التوقيع.
    - الكونفيغات: الأسطر الصريحة المطفأة في الثلاثة وقاعدة
      بند 50؛ لا CAID مسلّح فعليًا (REUSECAIDS فارغ تعليقًا).
    - التحقق على `bin/multics.x64` (بصمة
      ‏`c16eac1c74d84747b5c9f1b9f658e3d2`): الحزمة كلها +
      ‏cfgcheck ‏(5/5) + smoke خضراء.
    - ما تبقى من المرجع ولم يُبنَ: SF_CW_CHAIN/HALFREUSE
      ‏(مرفوضان بعلّة بند 54 — تخمين بنية شفرة)، حظر-IP-لكل-
      بروتوكول و`/loginlog` (هيكلة أكبر، بإشارة تشغيلية)،
      ‏SF_LATECW/SF_SUSPICIOUS (أصغر، عند الحاجة بدليل).

59. **LATECW: كاشف الشفرة المتأخرة عن عمر طلبها (2026-11-15)**:
    آخر كاشفات المرجع الصغيرة (SF_LATECW_MS، قسم 2.4) بمواصفة
    واضحة: الشفرة تصل بعد أن انتظر الـECM أطول من العتبة.
    العمر الحقيقي متاح عند نقطة الاستدعاء الحية في
    ‏`setdcw.c` ‏(`tick32_elapsed(now_ms, ecm->recvtime)`)
    فيُمرَّر للفحص الثالث عشر مع تجاوز `DCW LATECW` البروفايلي:
    - الأوضاع: `CACHE SF LATECW: 0|1|2` (افتراضي 0) وعتبة
      ‏`CACHE SF LATECWMS: 100..60000` (افتراضي 8000)؛ الرفض
      بسبب 13. عمر غير معروف ‏(`UINT32_MAX`، مسار غير
      ‏THREAD_DCW) لا يُعلَّم أبدًا — فشل مفتوح موثّق.
    - **تحذير تشغيلي مكتوب في الكود والكونفيغ معًا**: لا
      يُسلَّح عامًا أبدًا — بروفايلات المصدر البطيء (بطاقة
      تجيب بعشرات الثواني، وحالة 1814 الموثقة 60–150s)
      ستُسقط شفراتها الشرعية. التسليح لكل CAID فقط وبدليل
      تريس، بوضع التسجيل أولًا، كقاعدة بند 50 حرفيًا.
    - الاختبار `tests/latecw`: تحت العتبة يمرّ بكل الأوضاع،
      فوقها يُعلَّم بوضع 1 ويُرفض بسبب 13 بوضع 2، مجهول
      العمر آمن، عتبة 0 تُطفئ، والتجاوز بالاتجاهين — أخضر.
      (فشل أولي كان من فحص التكرار القائم بنفس اللحظة لا من
      الكاشف؛ فُصل بزمن متغير وسُجّل الدرس.)
    - **SF_SUSPICIOUS لم يُبنَ — قرار موثّق**: «إعادة استعمال
      على SIDs مختلفة» مغطاة أصلًا بفحص 8 ‏(DUPSEEN للكاملة)
      وبند 58 (للنصف)، و«نسبة سيئة أثناء الاشتباه» مغطاة
      بآلية `badcwratio` للنظير؛ بناء طبقة اشتباه ثانية
      سيعدّ الدليل نفسه مرتين. يُعاد فتحه فقط إن أظهر تريس
      نمطًا لا يمسكه الموجود.
    - بهذا يُغلق مرجع R200 عمليًا: نُفّذ 54 (إصلاحان)، 55
      ‏MARCHCHECK، 56 ‏CW_DUP، 57 حارس الويب، 58 ‏CW_REUSE،
      ‏59 ‏LATECW؛ ورُفض/وُثّق CHAIN/HALFREUSE/البذرة النصفية
      ‏وSUSPICIOUS؛ ويبقى بلا بناء بقرار: حظر-IP-لكل-بروتوكول
      و`/loginlog` (هيكلة صفحات/حظر أكبر، بإشارة تشغيلية من
      المشغّل) — كل المسلّح افتراضيًا: لا شيء؛ الكل OFF حتى
      دليل التريس لكل CAID.
    - التحقق على `bin/multics.x64` (بصمة
      ‏`868d9888c91bd04e7f991b24b221e6b1`): latecw + marchcheck
      + cwdup + cwreuse + loginban + camd35-guard + cfgcheck
      ‏(5/5) + smoke — الكل أخضر.

60. **حظر IP لكل بروتوكول (protoban) (2026-11-15)**: آخر بنود
    مرجع R200 الهيكلية (قسم 3.1) بتصميم محافظ يمدّ الموجود:
    حظر بند 57 كان HTTP فقط بدلالته، و`IPLIST` الخاصة بـCCcam
    **معطّلة أصلًا في بناء الإنتاج** (معلّقة في Makefile) —
    أي لم يكن لـCCcam أي حظر فشل حي. الجديد:
    - وحدة `protoban` داخل `loginthrottle.c` (نفس الساعة
      والقفل العائلي): لكل IP عدّاد فشل ومهلة حظر **لكل
      بروتوكول** (http/telnet/cccam/newcamd/mgcamd/cs378x/
      camd35/cacheex). فشل التلنت لا يحظر CCcam والعكس —
      العزل مختبَر صراحة. API: ‏note_failure ذرّية العتبة،
      ‏note_success (يمحو ذلك البروتوكول فقط)، banned،
      ‏unban (واحد أو الكل)، snapshot للحظر النشط، total.
    - سلكان حيان كاملان لا بنية ميتة: **التلنت**
      ‏(`TELNET LOGIN FAILS/BAN`، بوابة قبل المصادقة بجانب
      قائمة السماح، والنجاح يمحو) و**CCcam**
      ‏(`CCCAM LOGIN FAILS/BAN`): العتبة على محاولات دخول
      متتالية بلا نجاح (دلالة IPLIST نفسها، لكن صندوقية —
      قائمة IPLIST كانت لكل خادم على حدة فتدوير المنافذ
      يفلت منها)، بوابة قبول بجانب `isblockedip`، والنجاح
      يمحو. الافتراضي 0 = stock في البابين.
    - الرؤية: قسم «حظر حسب البروتوكول» في `/user-trace`:
      الإجمالي والنشط مع البروتوكول والـIP والمتبقي والعدّ.
      فكّ الحظر: انتهاء المدة، أو نجاح دخول، أو إعادة
      العتبة 0 (البوابة مشروطة بالكونفيغ الحي) — لا زرّ
      ‏`/iptables` منفصل (قرار بند 54 قائم: صفحة تشخيص
      واحدة). البروتوكولات الستة الباقية لها خانات في
      الجدول ولا باب يغذّيها بعد — مسماة دينًا لا إعلانًا
      ميتًا: تُسلك عند أول إشارة فشل قابلة للعدّ فيها.
    - الإثبات: `tests/protoban` (ساعة مُدخلة): عزل، عتبة،
      انقضاء، نجاح يمحو واحدًا فقط، unban جزئي/كلي،
      snapshot، وعتبة 0 stock — أخضر. وحيًا على الثنائي:
      تلنت بكونفيغ FAILS: 3 — ثلاث «wrong username» ثم
      الاتصال الرابع **يُغلق قبل أي سطر ترحيب**.
    - أخطاء بناء صُححت أثناء العمل وسُجّلت: موضع helper
      ‏CCcam (إعلان ضمني قبل التعريف، و`static` مكررة من
      لصق anchor) — درس: مواضع الـhelpers تُتحقق بترتيب
      الملف لا بافتراض بنية الدوال.
    - التحقق على `bin/multics.x64` (بصمة
      ‏`90e3edb5d6afcd01d3b9a98719252349`): protoban +
      ‏loginban + latecw + marchcheck + cwdup + cwreuse +
      ‏camd35-guard + cfgcheck ‏(5/5) + smoke — الكل أخضر.
    بهذا تُستكمل بنود مرجع R200 كلها قرارًا: منفّذة
    (54–60) أو مرفوضة بعلّة موثقة (CHAIN/HALFREUSE/بذرة
    ‏NDS/SUSPICIOUS/صفحة iptables منفصلة)، والباقي دين
    مسمى (تغذية protoban لبقية البروتوكولات، /loginlog
    كسجل محاولات لا صفحة مستقلة إن ظهرت حاجة).

61. **تغذية protoban لبابَي newcamd وcs378x (2026-11-15)**:
    سداد دين بند 60 المسمى بنفس النمط، عند نقطتَي فشل حقيقيتين
    لا عدّادات مصطنعة:
    - **newcamd** ‏(`NEWCAMD LOGIN FAILS/BAN` في فرع NEWCAMD
      العام): البوابة عند مدخل `cs_connect_cli_inner`، والتغذية
      عند «مستخدم مجهول» وعند «كلمة مرور خاطئة» (نقطتا الفشل
      الموثقتان جنب `nbloginerror`)، والمسح عند نجاح الاتصال.
      إثبات حي بأداة `ncclient`: ثلاث كلمات خاطئة، ثم الدخول
      **الصحيح** لا يتلقى حتى تسلسل الـ14 بايت الابتدائي؛
      وبلا تسليح يمرّ الدخول الصحيح بمراحله الست كلها.
    - **cs378x** ‏(`CS378X LOGIN FAILS/BAN`): البوابة قبل قراءة
      أي إطار، والتغذية عند UCRC مجهول (فشل الاعتماد الوحيد
      في هذا البروتوكول)، والمسح عند الاتصال المنتظم. إثبات
      حي مميز سلوكيًا: اتصال خامل قبل الحظر يبقى مفتوحًا
      (الخادم ينتظر الإطار 5s)، وبعد ثلاث إطارات UCRC تالفة
      يُغلق الاتصال الخامل **فورًا** (البوابة لا القراءة).
    - الافتراضي 0 = stock عند البابين، والأسطر الصريحة في
      الكونفيغات الثلاثة، والحالة تظهر في قسم protoban في
      ‏`/user-trace` كما هي.
    - **تصحيح ضمني اكتُشف أثناء العمل**: هدف اختبار بند 58
      سمّيته `cwreuse` فظلّل هدف TASK 1.3 الحي الأقدم في
      ‏`tests/stability.mk` (تحذير overriding recipe) —
      أُعيدت تسميته `cwreuse-half` مع تعليق، وعاد الهدفان
      يعملان دون تظليل. درس: أسماء أهداف الاختبارات تُفحص
      ضد stability.mk قبل الاختيار.
    - الباقي من خانات protoban بلا تغذية: mgcamd/camd35/
      ‏cacheex/http(داخل الجدول — HTTP له بابه الخاص من بند
      ‏57) — دين مسمى كما هو؛ تُغذّى عند نقطة فشل قابلة
      للعدّ أو تُترك، لا وسط.
    - التحقق على `bin/multics.x64` (بصمة
      ‏`cb9462d5a2bf667824015f1176e58e7e`): protoban +
      ‏loginban + latecw + marchcheck + cwdup + cwreuse-half
      + camd35-guard + cfgcheck ‏(5/5) + smoke + إثباتان
      حيان (newcamd/cs378x) — الكل أخضر.

62. **سجلّ محاولات الدخول (loginlog) (2026-11-15)**: بند
    المرجع ‏`/loginlog` ‏(4.2/3.3) لكن بهندستنا: قسم داخل
    ‏`/user-trace` لا صفحة مستقلة (قرار بند 54 — مصدر تشخيص
    واحد). حلقة 32 محاولة في `loginthrottle.c` ‏(قفلها
    الخاص): متى (ثواني unix)، الباب، الـIP، اسم المستخدم
    كما قُدّم (مقصوص 31 حرفًا)، ناجحة/فاشلة.
    - يُسجَّل فقط تحقق اعتماد حقيقي: في الويب نجاح Basic
      عند **سكّ الجلسة** تحديدًا (لا كل طلب حامل Basic،
      وإلا غمرت المتصفحات الحلقة)، والفشل عند 401 بشرط
      تقديم Authorization فعليًا (مسابير 401 العارية ليست
      محاولات). وفي التلنت عند الخطأين والنجاح، والاسم
      المحفوظ قبل قراءة كلمة المرور. أسماء المستخدمين
      تُهرَّب بـ`html_esc` عند العرض (بيانات لا markup).
    - إثبات حي كامل: ويب خاطئ ‏(401، فاشلة)، ويب صحيح
      ‏(200، ناجحة عند السكّ)، تلنت بمستخدم خاطئ (فاشلة) —
      والسجل في الصفحة أظهر الصفوف الأربعة بالتوقيت والباب
      والـIP والنتيجة، الأحدث أولًا كما صُمم.
    - `tests/loginaudit`: ترتيب الأحدث، التفاف الحلقة عند
      ‏32 (يبقى u41..u10)، قصّ الاسم، رفض بروتوكول غير
      صالح، وسلامة protoban/R3 جانبًا — أخضر (ASan/UBSan).
    - حدود معلنة: RAM فقط (إعادة التشغيل تمحوه كجلسات
      ‏R7)، وبابان فقط (newcamd/cs378x/cccam محاولاتها
      تُعدّ في protoban عند التسليح لكن لا تُسجَّل اسمًا —
      أسماء CCcam/newcamd متاحة تقنيًا وتركتها دينًا مسمى
      لا حاجة مثبتة بعد).
    - التحقق على `bin/multics.x64` (بصمة
      ‏`3b0a3db035ee98b7096d23f68c9242fd`): loginaudit +
      ‏protoban + loginban + cfgcheck ‏(5/5) + smoke +
      الإثبات الحي — الكل أخضر.

63. **تغذية mgcamd + أسماء المحاولات لكل الأبواب (2026-11-15)**:
    سداد الدينين المسميين من بندي 60/62:
    - **mgcamd** ‏(`MGCAMD LOGIN FAILS/BAN`): بوابة قبول بجانب
      ‏`isblockedip` في `mgcamd_srv_accept` (بمساعد snapshot
      مقفل كنمط CCcam)، تغذية عند «مستخدم مجهول» و«كلمة مرور
      خاطئة» في `mg_connect_cli`، مسح + تدقيق عند الاتصال.
      **حدّ الإثبات معلن**: لا عميل mgcamd في أدواتنا، فالحي
      المثبت هو بدء الخادم وقبول stock (أرسل keymod ‏14 بايت
      والويب 200)؛ التغذية/البوابة مراجعة كود وبناء نظيف
      ووحدة protoban مختبرة — لا ادعاء إثبات حي لبند mgcamd
      تحديدًا حتى يوجد عميل اختبار.
    - **أسماء المحاولات**: newcamd (مجهول/خاطئ/ناجح بـ
      ‏`cli->user`/`uname` — والثاني داخل نطاق كتلته فقط،
      التقطه المترجم)، cs378x (ناجح بالاسم، والمجهول بصيغة
      ‏`ucrc:XXXXXXXX` لأن لا اسم له)، CCcam (ناجح فقط —
      التغذية عند بدء الخيط قبل تحليل الاسم، ففشل CCcam
      يُعدّ في protoban بلا اسم؛ توثيق لا اختراع).
    - أخطاء أثناء العمل سُجلت دروسًا: إدخال مفسّر MGCAMD
      حطّ أولًا داخل فرع PROFILES (بحث رجعي توقف عند أول
      سطر فيه PROFILES) فنُقل، وكان سيكسر البناء لولا
      المترجم؛ و`uname` خارج النطاق التُقط قبل التشغيل.
      القاعدة المكررة: أي إدخال آلي يُتحقق منه ببناء فوري.
    - المتبقي من خانات protoban: camd35 (UDP بلا جلسة دخول
      — لا نقطة فشل نظيفة، يُترك دينًا بعلّة) وcacheex
      (أقران لا «دخول» بشري) وhttp داخل جدول protoban
      (بابه الخاص من بند 57 يعمل؛ توحيدهما مخاطرة بلا
      مكسب مثبت).
    - التحقق على `bin/multics.x64` (بصمة
      ‏`3d8102c5d7303fdc702caefda94dbd07`): الحزمة (8
      خضراء) + smoke + فحص mgcamd الحي الجزئي أعلاه.

64. **إثبات mgcamd حيًا (2026-11-16)**: سداد قيد بند 63
    «غير مثبت حيًا». اكتشاف عملي: mgcamd يستخدم نفس تأطير
    ‏`cs_message_*` واشتقاق `des_login_key_get` الخاصين
    بـnewcamd حرفيًا، فعميل الاختبار القائم `.ncclient.bin`
    يقوده دون أي كود جديد. `tests/mgban` (هدف جديد) يثبت:
    - محاولتان خاطئتان ثم صحيح ← `LOGIN_ACK` (دون العتبة)؛
    - ثلاث خاطئات ثم صحيح ← **لا keymod أصلًا** (الحظر عند
      القبول قبل أي بايت بروتوكول)؛
    - حلقة التدقيق في `/user-trace` تُظهر محاولات mgcamd
      بالاسم `mu1` ناجحة وفاشلة.
    - ملاحظة سلوك stock موثقة في الاختبار: mgcamd يُغلق
      المحاولات الخاطئة **دون إرسال NAK** (الإرسال معلّق في
      الكود)، فتظهر «لا رد» — ليست عطل اختبار.
    - لا تغيير في كود الإنتاج بهذا البند؛ الثنائي يبقى
      ‏`3d8102c5d7303fdc702caefda94dbd07`. التحقق: mgban +
      ‏protoban + loginaudit + loginban + cfgcheck ‏(5/5) +
      ‏smoke — الكل أخضر. بهذا لا تبقى أي تغذية protoban
      مثبّتة جزئيًا: الأبواب الخمسة المغذّاة كلها مثبتة حيًا
      أو بوحدة + حي، والمتبقي camd35/cacheex معلّل الرفض.

65. **عتبات SSF العددية لكل CAID (2026-11-16)**: تنفيذ مباشر
    لتوجيه «الكونفيغ والإعدادات بالتخصص لكل CAID». التدقيق
    أظهر أن أوضاع الفحوص (MARCHCHECK/CWDUP/CWREUSE/LATECW)
    كانت بروفايلية منذ بنود 55–59، لكن **عتباتها العددية
    بقيت عامة**: ‏`LATECWMS` ‏(8000ms عامة ضد مصادر 60–150s
    على CAIDs بعينها — نفس التحذير الذي من أجله مُنع تسليح
    LATECW عامًا)، ونافذة `CWREUSEWINDOW`، وعدّ ‏`CWDUPREP`.
    - حقول بروفايل جديدة (0 = وراثة العام) ومفاتيح داخل
      البروفايل: `DCW LATECWMS:` ‏(0..600000)،
      ‏`DCW CWREUSEWINDOW:` ‏(0..600000)، `DCW CWDUPREP:`
      ‏(0 أو 2..10 بعد القصّ)، تمرَّر عبر
      ‏`supersmartfilter_check` كوسائط (لا globals — درس
      سباقات عمال ECM)، وتفوز على العام في الاتجاهين.
    - الاختبارات توسّعت لا استُبدلت: latecw يثبت أن CAID
      بطيئًا بعتبة 200000 ينجو من رفض العام 8000 وأن CAID
      مشدّدًا بـ1000 يرفض عند 5s؛ cwdup بعتبة بروفايل 2
      يرفض حيث العام 10 صامت؛ cwreuse بنافذة بروفايل
      ‏120000 يعلّم مشاهدة +5s تَعُدّها نافذة العام 1000
      منتهية. الكل أخضر (ASan/UBSan على الإنتاجي).
    - إثبات حي: بروفايل `[ slowcaid ]` CAID 1814 بالمفاتيح
      الثلاثة يُقلع ويقبل دخول newcamd ‏(LOGIN_ACK) والويب
      ‏200 — المفسّر والحقول حية لا زخرفة.
    - ما بقي عامًا ومسمّى دينًا بوعي: ‏`SF TIME/BADCWRATIO/
      MINREPLIES/UNBLOCK/HISTORY` (إحصاءات نظير عالمية
      بطبيعتها — تخصيصها لكل CAID يحتاج جداول نظير×CAID
      لا حاجة مثبتة لها من التريس)، وعتبات MULTICHECK
      ‏(لها أصلًا نطاق `MC CAIDS`).
    - التحقق على `bin/multics.x64` (بصمة
      ‏`0d816a497a8ef8c6bf6e941b2036a7dc`): الحزمة كاملة
      ‏(protoban/loginaudit/loginban/latecw/marchcheck/
      cwdup/cwreuse-half/camd35-guard/cfgcheck 5/5/mgban)
      ‏+ smoke + الإثبات الحي — الكل أخضر.

66. **لماذا 1884 لا يجلب شفرة هنا ويفتح على نسخ أخرى (2026-11-16)**:
    شكوى ميدانية بتريس كامل، والسبب جذري ومثبت A/B حيًا:
    - التريس: كل محاولات 1884:1136 تموت عند ‏~1005ms
      ‏(«DCW timeout») بينما مصادرها Newcamd تردّ عند
      ‏1858–5279ms؛ الشفرات المتأخرة تُرفض بعدها
      ‏stale/recycled أو SSF-8. أي أن البطاقة **تردّ فعلًا**
      لكن بعد أن يكون السيرفر قد أعلن الفشل.
    - الآلية في `th-ecm.c`: ‏`ecm_get_effective_timeout()`
      يترك جدول البروفايدرات ‏(`ecm-provider-config.h`:
      ‏1884:000000 ‏`fallback_timeout = 1000`) يختصر الانتظار
      بصمت **ما لم** يصرّح البروفايل بـ`DCW TIMEOUT` (المفسّر
      يضبط `dcw.timeout_explicit` فيفوز الصريح). النسخ الأخرى
      بلا هذا الجدول فتنتظر افتراضيًا أطول فتصل الشفرة
      البطيئة داخل النافذة وتفتح القناة — لا فرق في الخطوط.
    - كونفيغاتنا المسلّمة كانت تحمل التعليق «خوادم بطيئة
      تحتاج وقت أطول» و`SERVER TIMEOUT: 5000` (يضبط نافذة
      الإرسال للخوادم لا مهلة إعلان فشل العميل) — المقبض
      الخطأ للمشكلة المقصودة.
    - الإصلاح (تخصص لكل CAID، بلا تغيير كود): ‏`DCW TIMEOUT:
      5000` صريحًا في بروفايل 1884 بالكونفيغين + expert.
    - `tests/slow1884` إثبات حي ببطاقة اختبار تردّ دائمًا
      بعد 2500ms: بلا السطر يفشل العميل عند ~1001ms،
      وبالسطر **تُسلَّم الشفرة نفسها**. أخضر.
    - تعليمات ميدانية مرافقة (من نفس التريس، لم تتغير):
      هجوم التخمين على mgcamd ‏(e1qs8h كل ~11 ثانية) ونجاح
      ‏`test999` عبر CCcam ما زالا بلا معالجة — تسليح
      ‏`MGCAMD/CCCAM LOGIN FAILS: 5` + `BAN: 3600` وإيقاف
      الحساب قرار المشغّل، والأدوات جاهزة منذ بنود 60–64.
    - لا تغيير في كود الإنتاج؛ الثنائي يبقى
      ‏`0d816a497a8ef8c6bf6e941b2036a7dc`. التحقق:
      ‏slow1884 + cfgcheck ‏(5/5) + smoke خضراء.

67. **تعميم درس 1884 على كل البروفايلات (2026-11-16)**: توجيه
    المشغّل «نركز في البروفايلات لنشغل القنوات باستقرار من
    الكاش والاسطر». التدقيق بأداة جديدة
    ‏`tools/profile-timeout-audit.py` (تحسب المهلة الفعلية لكل
    بروفايل: صريحة/مخنوقة بالجدول/افتراضية، وتفشل عند وجود
    مخنوق) أظهر **10 بروفايلات مخنوقة صامتًا** في كل كونفيغ
    مسلّم: 09BD ‏(1200)، 1830 ‏(1000)، 091F ‏(1000)، 1883
    ‏(1500)، 1811 ‏(1200)، 098D ‏(900)، 1819 ‏(1000)، 1810
    ‏(1300)، 1841 ‏(1000)، 09C4 ‏(350!) — أي خط أبطأ من هذه
    القيم محكوم عليه بالفشل قبل أن يردّ، تمامًا كـ1884.
    - الارتباط الميداني لافت ولا يُدّعى سببية كاملة: في تريس
      المشغّل كانت 09BD ‏0% (124 طلبًا)، 098D ‏0% (74)،
      ‏1810 ‏0% (72)، 1830 ‏20.5% بينما توأمها CWPAIR ‏1843
      عند 94.2% — الخنق يفسّر النمط ولا يلغي احتمال غياب خط
      عامل أصلًا عند بعضها.
    - الإصلاح: ‏`DCW TIMEOUT: 3000` صريحًا في العشرة
      بالكونفيغين (+expert) — 3000 هي افتراضية الكود نفسه
      ‏(config.c) فلا رقم مخترع، و1884 يبقى 5000 لدليله
      الأقوى. التدقيق الآن: 0 مخنوق من 91.
    - التحقق: cfgcheck ‏(5/5)، slow1884 أخضر، والكونفيغ
      الكامل يُقلع فعليًا (91 بروفايلًا، الويب يردّ 401
      مصادقة). لا تغيير كود إنتاج؛ الثنائي يبقى
      ‏`0d816a497a8ef8c6bf6e941b2036a7dc`.
    - للميدان: أضف السطر نفسه في بروفايلات كونفيغك الحي
      العشرة (منفذك وخطوطك تختلف عن المسلّم)، وأعد التحميل،
      وقارن نسب 09BD/098D/1810/1830 قبل/بعد.

68. **استقرار الكاش لكل CAID (2026-11-16)**: الخطوة الثانية من
    تركيز البروفايلات. معنى `CACHE TIMEOUT` في الكود: مهلة
    انتظار الكاش قبل الذهاب للخوادم — ويجب أن تكتمل داخل
    مهلة DCW وإلا جُوّع مسار الخادم عند غياب الكاش. التدقيق
    (موسّعًا أداة بند 67 بفحص هذا الثابت) وجد:
    - انعكاسات قاتلة: DigitalALB ‏0B00 بكاش 5000 > مهلة
      ‏3000، وSAT BG ‏0B01 بـ4000، وعائلة 0500 كلها بـ3000
      ‏= المهلة تمامًا — عند غياب الكاش لا يبقى للخوادم
      شيء داخل محاولة العميل.
    - نوافذ أقصر من زمن ردّ الكاش المقاس ميدانيًا (من
      تحليلات المشغّل نفسه 2026-11-16): 0B00 متوسط ردّ
      الكاش 905ms ونافذته 250، 0100 متوسطه 1206، 0604
      ‏615 مقابل 300...
    - الإصلاح لكل CAID بقيم مشتقة من تلك القياسات (المتوسط
      مقرّبًا للأعلى لمئة، ودائمًا دون مهلة DCW): 0B00←1000،
      ‏0100←1300، 0500←1000، 0B01←900، 0604←700،
      ‏1880←600، 1815←500، 0B02←800 (خادمه بطيء 1987ms
      فيحتاج الباقي) — 30 بروفايلًا في كل كونفيغ. لم يُلمس
      ما لا دليل عليه: 183D/183E (لا ضربات كاش أصلًا ونجاح
      الخطوط 85–92%) و1884/1814/1802/1843/4AEE (نوافذها
      تغطي مقاسها).
    - التحقق: التدقيق 0 مخالفات، cfgcheck ‏(5/5)،
      ‏slow1884 + smoke خضراء، والكونفيغ الكامل يُقلع
      (الويب 401). لا كود إنتاج؛ الثنائي يبقى
      ‏`0d816a497a8ef8c6bf6e941b2036a7dc`.
    - للميدان: طبّق قيم CAID نفسها في كونفيغك الحي وقارن
      نسبة الكاش العامة (42%) ونسب 0B00/0500/0100 بعدها.

69. **بوابة استبعاد الخوادم VALIDECMTIME (2026-11-16)**: الخطوة
    الثالثة للبروفايلات، وبطلب صريح «عدّل الملفات وارفع».
    في `loadbalance.c`: خادمٌ متوسطُ زمن ردّه ‏≥
    ‏`SERVER VALIDECMTIME` **لا يُختار أصلًا** — استبعاد كامل
    لا مجرد تأخير. بمقارنة أزمنة الخوادم المقاسة ميدانيًا
    (تحليلات المشغّل) بنوافذ البروفايلات وُجد 12 بروفايلًا
    تستبعد خوادمها العاملة بمتوسطاتها الحالية: 0B02 (700
    مقابل متوسط 1987!)، 1830 (1200 مقابل 2280)، 0100
    (800–1400 مقابل 1758)، 0B00 ‏(600/847)، 1880، 1803،
    ‏1861 — ونجاحاتها الميدانية المتدنية (0B02 ‏23%،
    ‏1830 ‏20.5%) متسقة مع بقاء الكاش/الأسرى فقط.
    - الإصلاح لكل CAID: ‏VALIDECMTIME = المتوسط المقاس
      مقرّبًا لأعلى 500 (سقف المفسّر 5000): 0B02←2000،
      ‏1830←2500، 0100←2000، 1803←2000، 1880←1500،
      ‏1861←1500، 0B00←1000 — في الكونفيغين + expert.
      ‏`SERVER TIMEOUT` (إيقاع إعادة الإرسال) تُرك عمدًا:
      تغييره مقايضة حمل لا بوابة إقصاء.
    - أثناء التحقق ظهر كسر ثابت بند 68 عند بروفايلين بلا
      قياس ميداني (Tring ‏0BAA وXTRA ‏0B0F: كاش 3000 =
      المهلة) فأُصلحا بـ1000 آمنة ومُعلنة بلا قياس.
      التدقيق الآن نظيف تمامًا (0 خنق، 0 كسر ثابت).
    - التحقق: cfgcheck ‏(5/5) + slow1884 + smoke + إقلاع
      الكونفيغ الكامل (401). لا كود إنتاج؛ الثنائي يبقى
      ‏`0d816a497a8ef8c6bf6e941b2036a7dc`.
    - للميدان: طبّق قيم VALIDECMTIME لقياساتك في كونفيغك
      الحي، وراقب خصوصًا 0B02 و1830 و0100 — إن بقيت
      منخفضة بعد رفع الإقصاء فالمشكلة خطوط ميتة لا نوافذ،
      وهذا بحد ذاته تشخيص نهائي لها.

70. **مصفوفة بروفايل × خاصية واصطياد تظليل ECM (2026-11-16)**:
    بطلب المشغّل تحليل كل خاصية في كل باقة لاستخراج كل
    الأخطاء (إعدادات أو نسخة). أُنجزت الجولة الأولى:
    - أداة جديدة `tools/profile-matrix-audit.py`: منافذ
      مكررة، مفاتيح ميتة (بلا فرع مفسّر)، قصّات صامتة،
      وثوابت بنود 67–68. نتائجها بعد إصلاحات 67–69: فئتان
      فقط — ‏`SERVER INTERVAL: 75` يُشغَّل 100 و`SERVER
      FIRST: 4` يُشغَّل 3 في ~49 بروفايلًا (عبر DEFAULT)،
      فصُحّح النص لما يُشغَّل فعلًا (لا تغيير سلوك، إزالة
      كذب كونفيغ). ولا مفاتيح ميتة ولا منافذ مكررة.
    - التدقيق الحي (`tests/profile-runtime-audit.py`،
      حُدّثت قوائمه لمفاتيح بنود 54–69) على 91 بروفايلًا
      كشف **21 بروفايلًا** فيها `ECM CHECK LENGTH: NO`
      مكتوب وYES مشغّلًا. السبب عيب نسخة حقيقي: فرع
      ‏`ECM` العام المبكر في `config.c` (لـDOUBLECHECK)
      كان يعترض كل سطر ECM فيبتلع مفاتيح البروفايل
      ‏(CHECK LENGTH وFINGERPRINT وLENGTH وHEADERWLST
      لم تُقرأ قط — درس «الفروع المكررة تحجب» يضرب
      مجدّدًا، وهذه المرة من الكود لا من إدخالاتنا).
    - الإصلاح: دمج الفرعين في فرع ECM واحد (DOUBLECHECK
      عامًا، والبقية للبروفايل)، بناء نظيف، وإثبات حي
      مزدوج: بروفايل بـ`ECM CHECK LENGTH: NO` يعرض NO
      الآن، و`ECM DOUBLECHECK: 1884 0500` العام ما زال
      يعمل (CAIDs مراقبة: 2). التدقيق الحي بعده: **91/91
      بلا أي mismatch**، والساكن 0 أخطاء.
    - أثر جانبي يجب أن يعرفه المشغّل: بعد هذا الإصلاح
      ستصبح أسطر `ECM LENGTH:` و`ECM HEADERWLST:`
      و`ECM FINGERPRINT:` في بروفايلاته **حية لأول مرة**
      — إن كانت قيمها قديمة فستبدأ بالتطبيق؛ راجعها قبل
      النشر (خصوصًا قوائم أطوال ECM).
    - التحقق على `bin/multics.x64` ببصمة جديدة
      ‏`01399be74b3fe073ee630ee30412d57a` (تغيّر كود):
      ‏cfgcheck ‏(5/5) + slow1884 + mgban + protoban +
      ‏loginaudit + loginban + smoke + تدقيقا البروفايلات
      — الكل أخضر.
    - حدود الجولة معلنة: المصفوفة تغطي مفاتيح البروفايل
      الظاهرة والقابلة للقياس؛ اختبار ECM/CW وظيفي لكل
      باقة على حدة (مصدر بطيء/سريع × كاش/خط) هو الجولة
      التالية المقترحة بنفس الريج (ncserver/ncclient).

71. **المصفوفة الوظيفية ECM/CW لكل باقة (2026-11-16)**:
    الجولة الثانية من بند 70 — من «المكتوب = المشغّل» إلى
    «المشغّل يعمل فعلًا». أداة جديدة دائمة
    (`tests/profile-functional-audit.py`، هدف
    `make -C tests funcmatrix BIN=../bin/multics.x64`):
    كل بروفايل من الـ91 يُنسخ حرفيًا من الكونفيغ المُسلّم
    (يُعاد كتابة PORT فقط) ويُقلع وحيدًا مع بطاقة
    ‏(‏.ncserver.bin) بـCAID وأول Provider للبروفايل وعميل
    ‏(‏.ncclient.bin)، بثلاث مراحل: بطاقة سريعة (60ms)،
    إعادة ECM نفسه على نفس الاتصال (مسار الكاش/إعادة
    الاستخدام)، بطاقة بطيئة (2500ms).
    - النتيجة: **66/91 سلّمت CW من البطاقة السريعة، وكلها
      سلّمت عند الإعادة أيضًا** (مسار الكاش/إعادة الاستخدام
      سليم في الـ66 كلها). من الـ66، **64 سلّمت حتى مع
      بطاقة 2500ms**؛ الاستثناءان Profile_BISS و
      Profile_Tandberg فشلا عند ~1037ms — مطابق لسقفهما
      الفعّال (~1000ms)، أي سلوك مهلة صحيح لا كسر.
    - الـ25 الباقية مصنّفة بالكامل، **0 غير مفسّرة**:
      (أ) **14 بروفايلًا `ECM-LENGTH-SYNTHETIC`**: قوائم
      ‏`ECM LENGTH` فيها لا تشمل 0x40 (طول ECM الاصطناعي
      64 بايت) فترفضه — مُثبت حيًا لـ091F برسالة
      ‏«Wrong ecm length». هذا هو تحذير بند 70 يتحقق:
      القوائم حية الآن؛ صوابها للـECM الحقيقية لا يُحسم
      إلا بتريس ميداني، والتصنيف هنا «قيد ريج» لا عيب.
      (ب) **11 بروفايلًا `PROVIDER-ROUTING-SYNTHETIC`**:
      ‏provid العميل الاصطناعي 0 والبطاقة تعلن provid
      غير صفري، فلا تُمرَّر الـECM أصلًا (تنتظر حتى
      المهلة) — مُثبت حيًا لـ0500/032830: نفس الإقلاع
      ببطاقة provid=000000 مرّر الـECM فورًا. الـECM
      الحقيقية تحمل موفّرها الصحيح، فهذا قيد ريج أيضًا.
      الأداة تخرج بـ2 فقط عند فشل غير مفسّر، فتصلح بوابة
      رجوع دائمة.
    - ملاحظتان من بناء الريج نفسه: (1) تسجيل دخول ثانٍ
      من نفس IP خلال ~10 ثوانٍ لا يُجاب (خنق stock ضد
      التخمين) — لذلك تتم الإعادة على نفس الاتصال
      ‏(NC_ECMS=2). (2) **دين تشخيص مسمّى**: سبب الرفض
      ‏«Wrong ecm length» لا يظهر في `/user-trace` (الصفحة
      تعدّ الطلب فاشلًا فقط، وتقرّ بأن بعض الأسباب غير
      مسجلة) — ظهر فقط في سجل ‏`-v`. إظهار سبب رفض الـECM
      في التريس مرشّح بند قادم، لم يُنفّذ هنا.
    - لا تغيير في كود الإنتاج ولا الكونفيغات في هذا
      البند؛ الثنائي يبقى `01399be7…`. التحقق:
      ‏cfgcheck ‏(5/5) + smoke + slow1884 خضراء، والجولة
      الكاملة 1177 ثانية على `bin/multics.x64`.

72. **تريس `/user-trace` يغطي عملاء newcamd وسبب رفض ECM
    (2026-11-16)**: دين بند 71 المسّمى يُغلق لعملاء
    newcamd. كان مسار التريس كله (محاولة/ربط/نتيجة)
    موجودًا في srv-cccam/freecccam فقط: عميل newcamd
    لا يظهر أصلًا في قائمة اختيار التريس، وبوابة بدء
    الجلسة ترفض أي ID ليس CCcam، ورفض `cs_accept_ecm`
    ‏(«Wrong ecm length»…) لا يُسجّل إلا في سجل debug.
    - الإصلاح (ثلاث نقاط، كلها opt-in بجلسة تريس نشطة
      ولا أثر عند غيابها): (1) `srv-newcamd.c`: بدء
      محاولة عند دخول ECM وتسجيل DENIED بسبب الرفض
      ‏(cs_accept_ecm و«too many ecm requests»)، وربط
      ‏trace_instance للـECM الجديد/المشترك كما في
      CCcam، وتسجيل نتيجة العميل عند الإرسال
      ‏(CW مُسلّمة/فشل/فشل إرسال) في cs_senddcw.
      (2) `httpserver.c`: قائمة الاختيار تضم عملاء
      ‏cs_client_data من كل البروفايلات بلاحقة
      ‏[newcamd]، وبوابة البدء تقبل
      ‏getnewcamdclientbyid. (3) `config.c`: عملاء
      newcamd يسحبون IDs من عدّاد CCcam نفسه (نفس
      إصلاح FreeCCcam السابق) حتى لا يتصادم ID عميلين
      مختلفي البروتوكول في مطابقة التريس بالـID وحده.
    - الإثبات الحي (`tests/newcamd-trace-live.py`، هدف
      ‏`nctrace`): مرحلة رفض — بروفايل بـ`ECM LENGTH: 42`
      والصفحة تُظهر «Wrong ecm length» لعميل newcamd
      مختار من القائمة؛ مرحلة تسليم — CW تصل وتظهر
      ‏«CW response sent» في التريس. ناجح أيضًا على
      بناء ASan. الثنائي الجديد
      ‏`057a9518ded266c5ce706b5652b916e0`.
    - ديون باقية مسمّاة: mgcamd وcs378x وcamd35 ما
      زالت بلا تسجيل ingress في التريس (عملاؤها بهياكل
      منفصلة)، وقائمة الاختيار لا تضمهم بعد؛ وأسباب
      فشل ECM المشترك العامة («ECM failed; reason may
      be unavailable») لم تتغيّر.

73. **اكتمال تغطية التريس: mgcamd وcs378x وcamd35 (2026-11-16)**:
    إغلاق دَين بند 72 المسّمى بنفس النمط تمامًا. المعالجات
    الثلاثة (`srv-mgcamd.c` و`srv-cs378x.c` و`srv-camd35.c`)
    حصلت على: بدء محاولة `/user-trace` عند دخول ECM،
    وتسجيل DENIED بأسبابها الحرفية («Invalid CAID»‏،
    «Invalid CAID/PROVIDER»‏، «SID not accepted»‏،
    «too many ecm requests» و«ecm length error» في
    mgcamd)، وربط trace_instance للـECM الجديد/المشترك،
    وتسجيل نتيجة العميل عند الإرسال (CW/فشل/فشل إرسال).
    قائمة اختيار التريس تضم الآن عملاء البروتوكولات
    الثلاثة بلاحقات [mgcamd]/[cs378x]/[camd35]، وبوابة
    بدء الجلسة تقبل معرّفاتهم؛ ومعرّفاتهم كلها سُحبت
    لعدّاد CCcam الواحد في `config.c` (خمسة مواضع) حتى
    لا يتصادم ID عبر البروتوكولات في مطابقة التريس.
    - الإثبات الحي: `tests/mgtrace-live.py` (هدف
      ‏`mgtrace`) — عميل mgcamd (نفس .ncclient.bin، فهو
      يشارك newcamd التأطير): رفض «Invalid CAID/PROVIDER»
      يظهر في التريس، وتسليم CW يظهر حدثه. ناجح على
      البناء العادي وASan. و`newcamd-trace-live` (بند 72)
      ما زال أخضر.
    - حدود معلنة: لا يوجد عميل اختبار cs378x/camd35 في
      الشجرة، فإثباتهما الحي اقتصر على ظهور عملائهما
      ومرآة الكود طريقًا بطريق؛ لم يُبنَ عميل cs378x
      جديد لهذا البند (دَين ريج مسمّى، لا دَين تريس).
    - الثنائي `ce6d71e486cefe88415e940dff5a09ac`.

74. **عميل اختبار cs378x/camd35 وإثبات تريسهما حيًا (2026-11-16)**:
    إغلاق دَين ريج بند 73. عميل جديد `tests/cs378xclient.c`
    مبني من `msg-camd35.c` الإنتاجي نفسه (نفس اشتقاق
    UCRC ومفاتيح AES بالتضمين، كفلسفة ncclient)، بهدف
    ‏`cxtrace`: لـcs378x اتصال TCP + دخول keepalive،
    ولـcamd35 حزمة UDP واحدة متصلة، وبنفس ECM اصطناعي
    64 بايتًا.
    - الإثبات (`tests/cs378xtrace-live.py`): لكلٍّ من
      cs378x وcamd35 مرحلتان — رفض «Invalid CAID/PROVIDER»
      ظاهر في `/user-trace` لعميل مختار من القائمة،
      وتسليم CW من بطاقة حقيقية بحدثه في التريس. أربع
      مراحل خضراء، وبذلك اكتمل إثبات بنود 72–73 حيًا
      لكل بروتوكولات العملاء الخمسة (CCcam/FreeCCcam
      أصلًا، newcamd، mgcamd، cs378x، camd35).
    - وجد UBSan أثناء فحص العميل: إزاحة `buf[0]<<24`
      على int في `cs378x_recv` (`src/msg-camd35.c:149`)
      سلوك غير معرّف للقيم ≥0x80 — أُصلحت بصبّ
      ‏uint32_t صريح (الموضع الوحيد في ذلك الملف؛ نفس
      النمط اللغوي موجود في ملفات أخرى ويُسمّى دَين
      تدقيق لاحق، لم يُمسّ). كود الآلة لم يتغيّر فبقيت
      بصمة الثنائي `ce6d71e4…` كما هي، والعميل بنسخة
      ASan/UBSan يجتاز المراحل الأربع نظيفًا بعده.
    - الرجوع: mgtrace وnewcamd-trace-live وcfgcheck
      وsmoke وslow1884 وcamd35-guard خضراء.

75. **مسح UB الإزاحات في كود الإنتاج (2026-11-16)**: دَين
    بند 74 المسّمى. النمط `buf[N]<<24` على int مُروَّج من
    ‏uint8_t سلوك غير معرّف عند القيم ≥0x80 (إزاحة يسارية
    تتجاوز بت الإشارة). المسح وجد **43 موضعًا** من نفس
    العائلة في 12 ملفًا (cacheex وcli-camd35/cccam/cs378x
    وclustredcache وconfig وmsg-camd35 وsrv-camd35/cccam/
    cs378x/freecccam) + 3 في `debug.c` (فئات/معرّفات تُزاح
    لنتيجة uint32_t) — كلها صُبّت `(uint32_t)` على العامل
    الأيسر فقط (الـ<<16 والـ<<8 ببايت لا UB فيهما). القيم
    الناتجة مطابقة سابقيًا (تكملة ثنائية)، والتغيّر الوحيد
    إزالة الـUB.
    - التحقق: بُني خادم UBSan كامل لوحدات main/parser/
      config (تضمّن فعليًا ملفات srv/cli) بـ
      ‏`-fsanitize=undefined -fno-sanitize-recover=all`
      مع `-fno-sanitize=alignment` فقط (فحص المحاذاة
      يصطدم بنيويًا بتصميم `-fpack-struct` للكود — موثّق
      منذ بنود ASan)، وأربع حزم حية مرّت عليه بلا أي
      إجهاض: newcamd-trace وmgtrace وcs378xtrace
      وdoublecheck-live. قبل الإصلاح كان العميل UBSan
      يجهض عند أول إطار (بند 74).
    - الثنائي الجديد `1d6c86186c2743829e1b59d9211b434c`
      (في مواضع تغيّر توليد الكود بين الإزاحة الموقّعة
      وغير الموقّعة)، وحزمة الرجوع الاعتيادية خضراء.

76. **باب protoban لبروتوكول camd35 (2026-11-16)**: دَين
    بنود 60–61 المسّمى. `PB_CAMD35` كان موجودًا في جدول
    الحظر منذ بند 60، وتعرضه `/user-trace` أصلًا عبر
    `protoban_snapshot`، لكن لا باب يغذّيه: camd35 بروتوكول
    UDP بلا اتصال، وهويته UCRC وحده (CRC32 على MD5
    المستخدم)، فكان تخمين الهوية غير محسوب أبدًا — بينما
    الأبواب الخمسة الأخرى (تلنت/CCcam/newcamd/cs378x/
    mgcamd) كلها محمولة منذ بنود 60–63.
    - الإعداد: `CAMD35 LOGIN FAILS/BAN` (افتراضي `0`/`3600`
      = OFF سلوك stock كاملًا)، بمفسّر وclamps
      ‏([0,1000]/[0,604800]) على نمط cs378x حرفيًا؛ مصرح
      بهما صراحة في ملفات الكونفيغ الثلاثة (0 = مطفأ).
    - الباب (`srv-camd35.c` في `camd35_recvmsg`): فحص
      `protoban_banned` مبكرًا بعد قراءة IP المصدر وقبل أي
      بحث عن عميل — المحظور تُسقط داتاגרاماته بصمت (UDP
      لا اتصال يُغلق). «الفشل» القابل للعدّ = إطار بـUCRC
      مجهول (لا عميل camd35 ولا cacheex يطابقه)، ويُسجَّل
      في حلقة تدقيق الدخول باسم `ucrc:xxxxxxxx` (بند 62
      يغطيه أصلًا بلا تغيير في httpserver). «النجاح» =
      إطار من عميل معروف يصفّر عدّ IP — على دلالة نجاح
      cs378x، وبدون قيد تدقيق لكل حزمة (لا حدث اعتماد
      في UDP لكل إطار).
    - الإثبات الحي (`tests/camd35ban-live.py` + هدف
      `make c35ban`، عميل cs378xclient بطور camd35):
      مرحلة A مسلّحة: 3 داتاغرامات بهوية خاطئة تُسقط
      الرابعة حتى بالمستخدم الصحيح، والحظر نشط ظاهر في
      `/user-trace` وسجلّ التدقيق فيه `ucrc:…`. مرحلة B
      غير مسلّحة (بلا المفتاحين): نفس الداتاغرامات الخاطئة
      لا أثر لها والمستخدم الصحيح يُخدَم بشفرة — stock.
    - البصمة الجديدة `6a954bebe40e8de95f178e75e4674d3a`.
      الرجوع الكامل أخضر: smoke وprotoban وloginaudit
      وloginban وmgban وcfgcheck وnctrace وmgtrace
      وcxtrace وcamd35-guard وslow1884.
    - الديون المسمّاة الباقية: protoban لبروتوكول
      cacheex (`PB_CACHEEX` بلا باب أيضًا)، وتوحيد حظر
      HTTP مع loginban (بند 57)، وتفصيل أسباب فشل ECM
      المشترك العامة في التريس.

77. **حفظ المحرّر لا يعاقب IP المشغّل (2026-11-16)**: بلاغ
    ميداني — المشغّل يعدّل الملف من المتصفح، ولديه جهاز
    (عميل CCcam) على نفس IP المصدر، فعند لحظة الحفظ يُطرَد
    الجهاز ويُحظَر الـIP مؤقتًا على بابَي CCcam والويب معًا.
    أُعيد الإنتاج حيًا قبل الإصلاح (جلسة ممسوكة + 10 محاولات
    التحام: 0/10 تُخدَم وحظر cccam نشط في `/user-trace`).
    ثلاثة أسباب جذرية، أُصلحت كلها:
    - **عدّ المحاولات بدل فشل الاعتماد في CCcam** (بند 60،
      دلالة IPLIST): كان `protoban_note_failure` يُستدعى عند
      كل اتصال قبل قراءة أي اعتماد، والنجاح وحده يصفّر.
      الريسيفر المطروح بإعادة التحميل يرتدّ إلى حارس منع
      التكرار (جلسته القديمة ما زالت «متصلة» حتى 60s) فتُرفَض
      محاولاته وتُعدّ كلها، ولا يستطيع النجاح ليصفّر، فيُحظَر
      الـIP المشترك. الآن تُعدّ فقط إخفاقات الاعتماد الحقيقية
      (مستخدم مجهول/كلمة مرور خاطئة/nodeid خاطئ) عبر
      `cccam_protoban_fail`، ورفض الجلسة المكررة — اعتماد
      مُتحقق منه — يُصفّر العدّ كأي نجاح. رفض السياسات
      (عميل معطّل/قائمة hosts) والمصافحات المجهضة لا تُعدّ.
      القوة الغاشمة ما زالت تقدّم اعتمادًا وتفشل هنا، فلم
      تضعف البوابة (المرحلة B2 من الاختبار تثبت الحظر).
    - **درع إعادة التحميل** (`login_reload_shield_*` في
      loginthrottle): أثناء `reread_config` ولمدة 10s بعده
      يتوقف عدّ الفشل على كل الأبواب (جدولا HTTP وprotoban)،
      والحظر القائم يبقى نافذًا والنجاح يصفّر كالعادة. خيط
      الإعداد وحده يرفع الدرع — إعادة التحميل فعل مشغّل، فلا
      نافذة مهاجم. يرفعه `th-cfg.c` حول إعادة القراءة.
    - **حفظ ذرّي في محرّر الويب**: كان `fopen(fname,"w")`
      يترك الكونفيغ مبتورًا على القرص طوال الكتابة. الآن
      ملف مؤقت بنفس المجلد + الحفاظ على صلاحيات الملف
      الأصلي + `rename()`. المراقب (inotify) يعامل الـrename
      ‏(IN_IGNORED/IN_DELETE_SELF) كتغيير فيعيد التحميل مرة
      واحدة، وأحداث rm_watch الذاتية تُصرَّف بعد كل إعادة
      تحميل كي لا تُقرأ كتغيير جديد.
    - الإثبات (`tests/reloadban-live.py` + `make relban`،
      يفشل على الثنائي السابق): A) جلسة CCcam ممسوكة
      (SIGSTOP داخل أول ECM) + حفظ محرّر حقيقي عبر POST +
      10 محاولات التحام مكررة — لا حظر، المتصفح 200 طوال
      الوقت، لا ملف .tmp متبقٍّ، وجلسة جديدة تُخدَم.
      B1) ست كلمات خاطئة داخل نافذة الدرع لا تُحظَر؛
      B2) بعد انقضاء الـ10s نفس الدفعة تحظر cccam فقط
      والويب مفتوح.
    - البصمة الجديدة `7d106971e1266c3971dedd02640ad12d`.
      الرجوع أخضر: الحزمة الاعتيادية + c35ban +
      cccam-reconnect.
    - ملاحظة ميدانية للمشغّل: من فعّل `CCCAM LOGIN FAILS`
      قبل هذا البند كان معرّضًا لهذا الحظر الكاذب عند كل
      حفظ؛ بعد التحديث آمن التفعيل بالقيم المقترحة
      (5/3600). الديون الباقية كما هي: protoban لـcacheex،
      وتوحيد حظر HTTP مع loginban (57)، وتفصيل أسباب فشل
      ECM المشترك في التريس.

78. **باب protoban لبروتوكول cacheex (2026-11-16)**: آخر دَين
    أبواب من بنود 60–63. `PB_CACHEEX` كان في جدول الحظر بلا
    باب يغذّيه. cacheex يدخل من أبواب مشتركة (CCcam
    مستخدم/كلمة مرور، وcs378x/camd35 بـUCRC)، فصار العدّ
    **قائمًا على الإسناد** وموثّقًا في الكود: لا يُعدّ فشل
    تحت PB_CACHEEX إلا إذا كانت الهوية المقدَّمة هوية
    cacheex مهيّأة فعلًا؛ الهويات المجهولة تبقى مع عدّاد
    بابها (تخمين غير مُسند لا يمكن تمييزه عن تخمين عميل).
    - الإعداد: `CACHEEX LOGIN FAILS/BAN` العالمي
      (افتراضي `0`/`3600` = OFF)، يخزَّن في `cfg->cache`،
      بمفسّر داخل فرع CACHEEX العام وclamps
      ‏([0,1000]/[0,604800])، ومصرّح به في الكونفيغات
      الثلاثة.
    - باب CCcam: مستخدم `cacheex_mode` بكلمة مرور خاطئة
      (أو nodeid خاطئ) يعدّ PB_CACHEEX؛ نجاحه يصفّره؛
      وإن كان محظورًا يُرفَض عند نقطة التعرف على هويته
      (المنفذ المشترك لا يمكن ترشيحه حسب الهوية عند
      القبول). المستخدم العادي من نفس IP لا يتأثر.
    - بابا cs378x/camd35: إطار UCRC يطابق قائمة cacheex
      يُسقَط إن كان الـIP محظورًا PB_CACHEEX، ونجاحه
      يصفّر PB_CACHEEX بدل عدّاد الباب؛ المجهول يبقى مع
      PB_CS378X/PB_CAMD35 (بندا 61/76).
    - الإثبات الحي (`tests/cacheexban-live.py` + هدف
      `make cxban`): مسلّحًا — 3 كلمات خاطئة لمستخدم
      cacheex تطلق الحظر، والصحيحة بعدها تُرفَض، والحظر
      ظاهر في `/user-trace` باسم cacheex، ولا حظر cccam،
      والمستخدم العادي من نفس IP يُخدَم بشفرة. غير
      مسلّح — نفس المحاولات بلا أثر والدخول الصحيح ينجح
      (stock).
    - البصمة الجديدة `67ffa76447dd8ff5cfa640ba1604c938`.
      الرجوع أخضر (الحزمة الاعتيادية + relban +
      cccam-reconnect). بذلك اكتملت أبواب protoban
      الثمانية كلها. الديون المسمّاة الباقية: توحيد حظر
      HTTP مع loginban (بند 57)، وتفصيل أسباب فشل ECM
      المشترك العامة في التريس.

79. **توحيد حظر HTTP مع protoban (2026-11-16)**: دَين بند
    57 المسّمى. كان باب الويب يحتفظ بجدول حظر خاص (خانات
    lt من بند 57) منفصل عن جدول protoban الموحّد: جدولان
    وعدّادا حالة، و`protoban_unban` لا يرفع حظر ويب. الآن
    HTTP على نمط التلنت حرفيًا (بند 60): جدول lt يعدّ فقط
    لأجل التأخير التصاعدي، والقرار والحظر في
    `PB_HTTP` داخل protoban — جدول واحد ولقطة واحدة في
    `/user-trace` ومسار رفع واحد.
    - التغييرات: إنفاذ ما قبل المصادقة بـ
      `protoban_banned(ip, PB_HTTP)` بدل `login_banned`؛
      الفشل بـ`protoban_note_failure(PB_HTTP)` بدل
      `login_note_failure_ex`؛ النجاح يصفّر الجدولين؛ سطر
      «حارس تخمين الويب» في `/user-trace` يقرأ الآن
      `protoban_total_proto/protoban_active` الجديدين
      (أضيفا إلى loginthrottle مع عدّادات لكل بروتوكول).
      واجهات lt القديمة باقية لوحدة الاختبار، بلا مستهلِك
      إنتاجي لقرار الحظر.
    - الإثبات الحي (`tests/httpban-live.py` + هدف
      `make hban`): مسلّح 3/4s — ثلاث كلمات خاطئة تحظر
      127.0.0.1، والصحيحة بعدها تُغلَق قبل المصادقة،
      والصفحة من 127.0.0.2 تُظهر `http — 127.0.0.1` في
      قائمة protoban الموحّدة و«محظور الآن: 1»؛ بعد
      الانتهاء يعود الدخول ويبقى «حظر إجمالي: 1». غير
      مسلّح = stock.
    - البصمة الجديدة `14d69c95667c8d579022ba668c2e9589`.
      الرجوع أخضر كاملًا (يشمل loginban/protoban
      الوحدويين وcxban وrelban).
    - الدَّين المسمّى الباقي وحده الآن: تفصيل أسباب فشل
      ECM المشترك العامة في التريس.

80. **تفصيل أسباب فشل ECM المشترك في التريس (2026-11-16)**:
    آخر دَين مسمّى. كان فشل محرك ECM المشترك النهائي
    (`ecm_faileddcw` في th-ecm.c) يرسم سطرًا عامًا واحدًا
    «ECM failed; reason may be unavailable» رغم أن كل
    مستدعٍ يعرف سببه بدقة — بل ثلاثة مستدعين كانوا يصدرون
    حدث FAILED الخاص بهم أولًا ثم يأتي العام مكررًا بعده.
    وبعده كانت أبواب العملاء الخمسة كلها تقول للعميل في
    التريس «decode failed; detailed reason unavailable».
    - `ecm_faileddcw(ecm, reason, elapsed)` الآن: يستقبل
      سبب المستدعي (Invalid profile id / maximum servers
      reached / DCW timeout / no free server available /
      server dispatch window expired) ويصدر FAILED مرة
      واحدة به، وحُذفت الإصدارات المكررة الثلاثة.
    - لقطة التسليم (`ledger_delivery_snapshot`) تحمل
      الآن `statusmsg[96]` منسوخًا عند الالتقاط (الخانة
      تُعاد تدويرها فلا يصح مؤشر)، ومواقع نتيجة العميل
      الخمسة (cccam/newcamd/mgcamd/cs378x/camd35) تعرض
      «decode failed (<السبب>)» بدل العام.
    - الإثبات الحي (`tests/ecmfail-trace-live.py` + هدف
      `make ecmfail`): بروفايل بلا أي خط خادم — ECM
      newcamd يفشل في المحرك المشترك فقط؛ التريس يُظهر
      السبب المفصّل ونتيجة العميل المفصّلة، ولا أثر
      لأيٍّ من السلسلتين العامتين.
    - البصمة الجديدة `81bf8cdef22ca36634ea9aedcc2ec5af`.
      الرجوع أخضر كاملًا (+ cccam-reconnect).
    - **بذلك تُستنفد الديون المسمّاة كلها** (protoban
      بثمانية أبواب، توحيد حظر HTTP، أسباب فشل ECM).
      المتبقي المعروف من الذاكرة التشغيلية: توصيات
      ميدانية بلا قرار مشغّل (تسليح MGCAMD/CCCAM ضد
      `e1qs8h` صار آمنًا بعد بند 77)، ومراقبة
      DoubleCheck ‏(13/2928)، وCWPAIR ‏1830↔1843
      بانتظار تأكيد ميداني.

81. **تحقق ASan حي لكود بنود 76–80 (2026-11-16)**: لا تغيير
    كود — تحقق فقط، وفق قاعدة المشروع (ASan/UBSan للكود
    الإنتاجي الجديد). بُني `make-x64/x64/multics-asan`
    (وحدات main/parser/config مُعقّمة، وبها srv-cccam/
    th-ecm/th-cfg والسيرفرات المضمّنة) وشُغّلت عليه مع
    `LD_PRELOAD=libasan` و`ASAN_OPTIONS=halt_on_error=1`
    خمس حزم حية تمسّ المسارات الجديدة مباشرة:
    relban (77: عدّ CCcam الاعتمادي + درع إعادة التحميل +
    الحفظ الذرّي)، وcxban (78: باب cacheex)، وhban
    (79: توحيد PB_HTTP)، وecmfail (80: أسباب فشل ECM)،
    وc35ban (76: باب camd35) — كلها PASS بلا أي تقرير
    معقّم (أي تقرير كان سيُجهض الخادم ويفشل الاختبار).
    وحدة `test_loginthrottle`: ‏25/25. الثنائي الإنتاجي
    لم يتغيّر (البصمة `81bf8cdef22ca36634ea9aedcc2ec5af`
    قائمة).

82. **تأكيد CWPAIR ومراقبة DoubleCheck من CCcam Trace
    (2026-11-16)**: توجيه المشغّل — المراقبة والتأكيد كله
    من `/user-trace` حصرًا. كانت قائمة الأزواج المكتشفة
    كتل نسخ عارية: بلا provider ولا حداثة ولا علم إن كان
    بروفايل يحمل الزوج أصلًا، فتأكيد زوج مثل 1830↔1843
    من الصفحة كان تخمينًا.
    - صفّ الزوج الآن (الأكثر تأكيدًا أولًا): القناتان
      كاملتين `CAID:PROVIDER:SID`، عدد التطابقات، عمر
      آخر تطابق، وحالة التهيئة (اسم البروفايل الحامل +
      CWPAIR ACTIVE)؛ كتلة النسخ تظهر فقط لغير المُهيّأ.
      لقطة `cwpair_snapshot` تصدّر `last_tick` وترتّب
      بالأهمية (matches ثم الحداثة).
    - سطر DoubleCheck يضيف نسبة عدم التطابق كما يقرأها
      الميدان: `نسبة عدم التطابق: X% (fail من total)`.
    - **عيب اكتُشف أثناء الإثبات**: مسار الالتزام
      `trusted_deliver` في setdcw.c (مصادر البطاقات/
      cacheex عبر الطابور الموسوم) لم يكن يغذّي
      `cwpair_observe` أصلًا — فقط المسار القديم ومخازن
      الكاش — فاكتشاف CWPAIR كان أعمى عن شفرات البطاقات،
      ولا زوج يتعلّم لبروفايلات مغذّاة ببطاقات. أُضيف
      نفس الاستدعاء عند الالتزام.
    - الإثبات الحي (`tests/cwpair-confirm-live.py` +
      هدف `make cwconfirm`): بروفايلا 183D/183E ببطاقتين
      تجيبان SID واحد بنفس الشفرة؛ الصفحة تُظهر الزوج
      بهويتيه وعدد تطابقاته وحداثته و«مُهيّأ أصلًا في
      بروفايل [pair183e] — CWPAIR ACTIVE: NO»، وسطر
      نسبة DoubleCheck حاضر. اختبار cwpair-live الأصلي
      ما زال أخضر.
    - البصمة الجديدة `bbc353d79d3bdd13603fec87cbf73fe6`.
      الرجوع أخضر كاملًا.

83. **توحيد حالة SuperSmartFilter + توثيق أبواب الدخول (بند 83)**:
    - **العيب الجذري**: كل حالة SSF (الجداول والمؤشرات
      و`ssf_stats` و`ssf_lock`) كانت `static` داخل
      `supersmartfilter.h`، فامتلكت كل وحدة ترجمة نسختها
      الخاصة: الفحوص عملت على نسخة main.o بينما صفحة
      `/smartfilter` قرأت نسخة httpserver.o الصفرية —
      العدّادات «المقفلة» لم تكن تُقرأ أصلًا. حُوّلت الحالة
      إلى `extern` (نمط `ssf_cfg` القائم) بتعريف وحيد في
      main.c، وتعريفات اختبارية في وحدات الاختبار الخمس.
    - **الإثبات الحي** (`tests/ssfconsole-live.py` + هدف
      `make ssfconsole`): FILTER: 1 على بروفايل 1884 وبطاقة
      تجيب → `/smartfilter` تُظهر `Total Checked=1` (كان
      مستحيلًا بنيويًا قبل الإصلاح). اختبارات marchcheck/
      cwdup/cwreuse/latecw وtest_supersmartfilter_ticks خضراء.
    - **autorecov**: يبقى مراقبة فقط — موثّق الآن صراحة في
      CONFIG-REFERENCE (`AUTO RECOVER` بلا مستهلِك قرار)؛
      توصيل مستهلِك آلي غير مُثبت ميدانيًا رُفض محافظةً.
    - **CONFIG-REFERENCE**: قسم 6.5 جديد يوثّق مفاتيح
      `<PROTO> LOGIN FAILS/BAN` للأبواب الثمانية (HTTP/
      TELNET/CCCAM/NEWCAMD/CS378X/MGCAMD/CAMD35/CACHEEX)
      بمداها وافتراضياتها وسجل `/user-trace`.
    - **ديون مسمّاة لا تُدّعى محلولة**: (أ) المفتاح العالمي
      `CACHE SF LEVEL` مظلَّل بفرع مفسّر أقدم فلا يصل
      filter_level (الفعّال هو `FILTER:` داخل البروفايل)؛
      (ب) أهداف cache-check-characterization/cache-cross-*
      تنكسر في بناء ASan الخاص بها عند srv-newcamd.c
      (`cli` undeclared) — مُتحقق أنه عطل سابق على HEAD
      النظيف وليس من هذا البند؛ (ج) هدف stability يحتاج
      ثنائي stock غير المبني في هذه البيئة (فرع configured
      أخضر).
    - البصمة الجديدة `6f7c228880e7bb21fb28c3f7ba6782e7`.
      الرجوع: الحزمة الكاملة (عدا الاستثناءات المسمّاة
      أعلاه) خضراء.

84. **إحياء مفاتيح `CACHE SF` العالمية (بند 84)**:
    - تصحيح تشخيص بند 83: المفتاح لم يكن مظلَّلًا بفرع
      أقدم؛ العيب طبقتان: (1) فرع SF في المفسّر اشترط
      `:` أو `=` مباشرة بعد SF فتجاهل الصيغة الموثّقة
      `CACHE SF LEVEL: n`، و(2) `supersmartfilter_init()`
      كانت تُستدعى بعد قراءة الإعداد فتمسح `ssf_cfg`
      كاملة ثم يُفرض `filter_level = 0` صراحة — فكل
      مفاتيح SF العالمية في ملفات الإنتاج الجاهزة كانت
      ميتة عند الإقلاع مهما كُتب.
    - الإصلاح: المفسّر يقبل الصيغتين (`CACHE SF X: n`
      و`CACHE SF: X: n`)، ونُقلت التهيئة إلى ما قبل
      `start_thread_config()` مع فرض `filter_level = 0`
      هناك كافتراضي آمن — الافتراضي يبقى OFF تمامًا،
      ولا يعمل أي فحص إلا بمفتاح مشغّل صريح (عالمي أو
      `FILTER:` لكل بروفايل).
    - الإثبات الحي (ssfconsole بطورين): بمفتاحي
      `CACHE SF LEVEL: 1` + `CACHE SF TIME: 777` تُظهر
      الصفحة Level 1 وWindow 777ms وTotal Checked=1؛
      وبدون أي مفتاح يبقى Level 0 وChecked=0. وحدات
      SSF وmarchcheck/cwdup/cwreuse/latecw وcfgcheck
      والرجوع (smoke/r48-web/prof-full/reload) أخضر.
    - تنبيه تشغيلي: من كان يعتمد على `CACHE SF LEVEL: 2`
      في ملف جاهز (multics-full-30w-52e.cfg) سيجد الفلتر
      يعمل فعليًا بعد هذا البند كما هو مكتوب — هذا هو
      السلوك الموثّق، مع بقاء البوابات الفرعية
      (MARCHCHECK/CWDUP/CWREUSE/LATECW) مطفأة ما لم
      تُفعّل مفاتيحها هي أيضًا.
    - البصمة الجديدة `2f42f4c5f6d2053280840dd46d809cae`.
