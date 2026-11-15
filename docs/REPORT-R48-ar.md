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
