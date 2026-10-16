# دليل النشر — MultiCS r82a المُحصَّن (2026-10-14)

هذا الدليل يشرح كيفية نشر الخادم المُحصَّن في بيئة إنتاجية، من المتطلبات حتى المراقبة اليومية.

---

## 1. المتطلبات

| المتطلب | الحد الأدنى | الموصى به |
|---|---|---|
| **النظام** | Linux x86_64 (kernel 4.x+) | Debian 11+ / Ubuntu 20.04+ |
| **المعالج** | 1 core | 2+ cores |
| **الذاكرة** | 64 MB | 256 MB+ |
| **الشبكة** | 10 Mbps | 100 Mbps+ |
| **المترجم (للبناء)** | gcc 10+ | gcc 14.2.0 |
| **أدوات البناء** | GNU Make 4.x | GNU Make 4.4.1 |

**ملاحظة:** الثنائيات المُسلَّمة (`bin/multics-r82a-*-x64`) مُجمَّعة مسبقاً ولا تحتاج مترجماً للتشغيل.

---

## 2. التثبيت

### 2.1 من الثنائيات المُسلَّمة (موصى به)

```bash
# انسخ الثنائيات إلى مجلد النشر
mkdir -p /opt/multics/bin
cp bin/multics-r82a-stats-x64 /opt/multics/bin/multics
chmod +x /opt/multics/bin/multics

# انسخ ملفات الإعداد
cp multics-ready.cfg /opt/multics/multics.cfg
cp start-multics.sh /opt/multics/
chmod +x /opt/multics/start-multics.sh
```

### 2.2 من المصدر

```bash
cd make-x64
make release-stats          # يُنتج bin/multics-r82a-stats-x64
cp x64/multics ../bin/multics-r82a-stats-x64
```

**تحذير:** `make -C make-x64` (بدون `release-*`) يبني نكهة stock العادية ويمسح `x64/` في النهاية. استخدم دائماً `release-stats` أو `release-queue-stats`.

---

## 3. الإعداد

### 3.1 الملف الأساسي

عدّل `/opt/multics/multics.cfg`:

```cfg
# واجهات الإدارة
HTTP PORT: 5500
HTTP USER: admin
HTTP PASS: كلمة_مرور_قوية

TELNET PORT: 5501
TELNET USER: admin
TELNET PASS: كلمة_مرور_قوية

# عنوان الخادم
HTTP TITLE: MultiCS-Production

# شبكة الكاش
CACHE PORT: 21000
CACHE PEER: 10.0.0.1:21000 { csp=1 }
CACHE PEER: 10.0.0.2:21000 { csp=1 }

# خيارات R17 (oscam compat) — لكل نظير
CACHE PEER: 10.0.0.3:21000 { cacheex_cwcfwd=1 cacheex_rcfilter=1 cacheex_nodeloop=1 }

# خيارات R18 (multi-hop relay)
CACHEEX MAXHOP_LG: 4

# البروفايلات
[ profile1 ]
CAID: 1884
PROVIDERS: 000000, 002008
PORT: 17000
USER: user1 pass1

[ profile2 ]
CAID: 09C4
PROVIDERS: 000000
PORT: 17001
USER: user2 pass2
```

### 3.2 المفاتيح الحرجة

| المفتاح | الوظيفة | الافتراضي |
|---|---|---|
| `HTTP PORT` | منفذ واجهة الويب | 5500 |
| `HTTP USER/PASS` | بيانات دخول الويب | — |
| `TELNET PORT` | منفذ تيلنت | 5501 |
| `CACHE PORT` | منفذ استقبال الكاش | — |
| `CACHE PEER` | نظير كاش (يمكن تكراره) | — |
| `CAMD35 PORT` | منفذ camd35 للخوادم | — |
| `DCW STATS: ON` | تفعيل صفحة `/cwlog` | OFF |
| `CACHE FILTER: ON` | فلترة الكاش | ON |
| `CACHE LATE-TRUTH: ON` | تفعيل R13 (تصحيح الحقيقة المتأخرة) | OFF |
| `PEER-TRUST-STORE: ON` | تفعيل R14 (حارس التلوث) | OFF |
| `CACHEEX MAXHOP_LG: N` | أفق القفزات المتعددة (1..10) | 2 (stock) |

---

## 4. التشغيل

### 4.1 التشغيل اليدوي

```bash
/opt/multics/bin/multics -C /opt/multics/multics.cfg
```

### 4.2 التشغيل كخدمة systemd

أنشئ `/etc/systemd/system/multics.service`:

```ini
[Unit]
Description=MultiCS r82a Hardened
After=network.target

[Service]
Type=simple
User=multics
Group=multics
WorkingDirectory=/opt/multics
ExecStart=/opt/multics/bin/multics -C /opt/multics/multics.cfg
Restart=on-failure
RestartSec=5
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

ثم:

```bash
sudo systemctl daemon-reload
sudo systemctl enable multics
sudo systemctl start multics
sudo systemctl status multics
```

### 4.3 التحقق من التشغيل

```bash
# HTTP
curl -u admin:password http://localhost:5500/

# Telnet
echo "help" | nc localhost 5501

# JSON API
curl -u admin:password http://localhost:5500/json | python3 -m json.tool
```

---

## 5. المراقبة

### 5.1 الصفحات المهمة

| الصفحة | الوظيفة | النكهة المطلوبة |
|---|---|---|
| `/` | الصفحة الرئيسية | أي |
| `/profiles` | حالة البروفايلات | أي |
| `/cwlog` | سجل CW + صحة القنوات + cacheex | stats فقط |
| `/json` | API JSON شامل | أي |
| `/dcwstats` | عدادات DCW | stats فقط |

### 5.2 أوامر تيلنت المهمة

```bash
# الاتصال
nc localhost 5501

# الأوامر
help              # قائمة الأوامر
dcwstats          # عدادات DCW
chanhealth        # صحة القنوات
CACHEEX           # عدادات cacheex لكل نظير
profiles          # حالة البروفايلات
```

### 5.3 السجلات

الخادم يكتب إلى `stdout`/`stderr`. في systemd:

```bash
sudo journalctl -u multics -f          # متابعة حية
sudo journalctl -u multics --since "1 hour ago"  # آخر ساعة
```

---

## 6. التحديث

### 6.1 تحديث الثنائيات

```bash
# أوقف الخادم
sudo systemctl stop multics

# استبدل الثنائية
cp /path/to/new/multics-r82a-stats-x64 /opt/multics/bin/multics
chmod +x /opt/multics/bin/multics

# أعد التشغيل
sudo systemctl start multics
```

### 6.2 التحقق من البصمات

```bash
md5sum /opt/multics/bin/multics
# قارن مع MANIFEST-md5 في المستودع
```

---

## 7. الأمان

### 7.1 المبادئ الأساسية

- **غيّر كلمات المرور الافتراضية** فور التثبيت.
- **قيّد الوصول** إلى منافذ HTTP/telnet عبر firewall.
- **لا تُشغّل الخادم كـ root** — استخدم مستخدم مخصص (`multics`).
- **راقب السجلات** يومياً للشذوذ.

### 7.2 Firewall (iptables)

```bash
# السماح بالوصول من شبكة الإدارة فقط
iptables -A INPUT -p tcp --dport 5500 -s 10.0.0.0/24 -j ACCEPT
iptables -A INPUT -p tcp --dport 5501 -s 10.0.0.0/24 -j ACCEPT
iptables -A INPUT -p tcp --dport 5500 -j DROP
iptables -A INPUT -p tcp --dport 5501 -j DROP

# السماح بنظراء الكاش
iptables -A INPUT -p tcp --dport 21000 -s 10.0.0.1 -j ACCEPT
iptables -A INPUT -p tcp --dport 21000 -s 10.0.0.2 -j ACCEPT
iptables -A INPUT -p udp --dport 21000 -j ACCEPT
```

---

## 8. النسخ الاحتياطي

```bash
# الإعدادات
cp /opt/multics/multics.cfg /backup/multics.cfg.$(date +%Y%m%d)

# السجلات (إذا كانت تُحفظ)
cp /var/log/multics.log /backup/multics.log.$(date +%Y%m%d)
```

---

## 9. استكشاف الأخطاء

انظر **[TROUBLESHOOTING-ar.md](TROUBLESHOOTING-ar.md)** للمشاكل الشائعة وحلولها.

---

## 10. الدعم

- **المستندات:** [docs/INDEX-ar.md](INDEX-ar.md)
- **الحالة:** [STATUS.md](STATUS.md)
- **التقارير:** `docs/REPORT-*-ar.md`
