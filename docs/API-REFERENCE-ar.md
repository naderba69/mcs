# مرجع API — HTTP و Telnet (2026-10-14)

مرجع شامل لواجهات الإدارة: HTTP (صفحات ويب + JSON) و Telnet (أوامر نصية).

---

## 1. HTTP API

### 1.1 المصادقة

كل الطلبات تتطلب Basic Auth:

```bash
curl -u admin:password http://localhost:5500/json
```

### 1.2 الصفحات الأساسية

| المسار | الوظيفة | النكهة | المثال |
|---|---|---|---|
| `/` | الصفحة الرئيسية | أي | `curl http://host:5500/` |
| `/profiles` | حالة البروفايلات | أي | `curl http://host:5500/profiles` |
| `/clients` | العملاء المتصلون | أي | `curl http://host:5500/clients` |
| `/servers` | حالة الخوادم | أي | `curl http://host:5500/servers` |
| `/cachepeers` | نظراء الكاش | أي | `curl http://host:5500/cachepeers` |

### 1.3 صفحات الإحصاءات (stats flavour فقط)

| المسار | الوظيفة | المحتوى |
|---|---|---|
| `/cwlog` | سجل CW + صحة القنوات + cacheex | جدول HTML |
| `/dcwstats` | عدادات DCW | جدول HTML |

**تحذير:** هذه الصفحات تُرجع HTTP 000 (connection closed) في نكهة stock — ليست bug، بل `#ifdef MCS_DCSTATS`.

### 1.4 JSON API

**`GET /json`** — يُرجع كل البيانات بصيغة JSON:

```bash
curl -u admin:password http://localhost:5500/json | python3 -m json.tool
```

**البنية:**

```json
{
  "status": "running",
  "uptime": 3600,
  "profiles": [
    {
      "name": "card1",
      "caid": "1884",
      "providers": ["000000", "002008"],
      "port": 17000,
      "clients": 5
    }
  ],
  "clients": [
    {
      "user": "user1",
      "ip": "10.0.0.5",
      "port": 12345,
      "caid": "1884",
      "sid": "0065"
    }
  ],
  "cachepeers": [
    {
      "host": "10.0.0.1",
      "port": 21000,
      "csp": true,
      "push": 150,
      "relay": 120,
      "drop": 10,
      "filter": 5,
      "loop": 2
    }
  ],
  "chanhealth": [
    {
      "caid": "1884",
      "provid": "000000",
      "sid": "0065",
      "delivered": 100,
      "refused": 5,
      "held": 2,
      "dying": false
    }
  ],
  "cacheex": [
    {
      "peer": "10.0.0.1:21000",
      "origin": "cacheex-server",
      "push": 150,
      "relay": 120,
      "drop": 10,
      "filter": 5,
      "loop": 2,
      "last_event": "drop",
      "last_reason": "profile-filter",
      "last_caid": "1884",
      "last_provid": "000000",
      "last_sid": "0065",
      "last_tick": 1234567890
    }
  ]
}
```

**الحقول المهمة (R20):**
- `cacheex[].push/relay/drop/filter/loop` — عدادات cacheex لكل نظير
- `cacheex[].last_event/last_reason` — آخر حدث مسمّى (drop/filter/loop + اسم القاعدة)
- `cacheex[].last_caid/last_provid/last_sid` — القناة المعنية
- `chanhealth[].dying` — هل القناة "تموت بالرفض" (streak ≥ 5)

---

## 2. Telnet API

### 2.1 الاتصال

```bash
nc localhost 5501
# أو
telnet localhost 5501
```

**المصادقة:**

```
admin
password
```

### 2.2 الأوامر الأساسية

| الأمر | الوظيفة | المثال |
|---|---|---|
| `help` | قائمة الأوامر | `help` |
| `status` | حالة الخادم | `status` |
| `profiles` | حالة البروفايلات | `profiles` |
| `clients` | العملاء المتصلون | `clients` |
| `servers` | حالة الخوادم | `servers` |
| `cachepeers` | نظراء الكاش | `cachepeers` |
| `exit` / `quit` | إنهاء الجلسة | `exit` |

### 2.3 أوامر الإحصاءات

| الأمر | الوظيفة | النكهة |
|---|---|---|
| `dcwstats` | عدادات DCW | stats |
| `chanhealth` | صحة القنوات (R15) | stats |
| `CACHEEX` | عدادات cacheex لكل نظير (R20) | stats |
| `cwlog` | آخر CWs | stats |

### 2.4 إخراج `dcwstats`

```
dcwstats: ON
delivered 150
refused 25
held 10
cache_hit 120
cache_miss 30
```

### 2.5 إخراج `chanhealth` (R15)

```
chanhealth:
1884:000000:0065 delivered=100 refused=5 held=2 dying=no
1884:000000:0066 delivered=0 refused=10 held=0 dying=yes
```

**`dying=yes`** = streak ≥ 5 رفضات متتالية منذ آخر تسليم (شاشة سوداء محتملة).

### 2.6 إخراج `CACHEEX` (R20)

```
CACHEEX:
10.0.0.1:21000 cacheex-server push 150 relay 120 drop 10 filter 5 loop 2 | last drop: profile-filter 1884:000000:0065
10.0.0.2:21000 csp push 80 relay 70 drop 0 filter 0 loop 0 | last push: (unnamed) 0000:000000:0000
```

**التنسيق:**
```
peer origin push N relay N drop N filter N loop N | last event: reason caid:provid:sid
```

**الحقول:**
- `peer` — عنوان النظير
- `origin` — نوع الاتصال (`cacheex-server`, `csp`, `cccam-client`, إلخ)
- `push/relay/drop/filter/loop` — العدادات الخمسة
- `last event` — آخر حدث (`push`, `relay`, `drop`, `filter`, `loop`)
- `reason` — اسم القاعدة (مثل `profile-filter`, `nodeloop`, `pushfilter`) أو `(unnamed)`
- `caid:provid:sid` — القناة المعنية

---

## 3. أمثلة استخدام

### 3.1 مراقبة cacheex من سكريبت

```bash
#!/bin/bash
# مراقبة cacheex drops
JSON=$(curl -s -u admin:password http://localhost:5500/json)
DROPS=$(echo "$JSON" | jq '[.cacheex[].drop] | add')
echo "Total drops: $DROPS"

# تنبيه إذا تجاوز الحد
if [ "$DROPS" -gt 100 ]; then
    echo "ALERT: High drop count!" | mail -s "MultiCS Alert" admin@example.com
fi
```

### 3.2 مراقبة صحة القنوات

```bash
#!/bin/bash
# البحث عن قنوات "تموت"
DYING=$(echo "chanhealth" | nc localhost 5501 | grep 'dying=yes')
if [ -n "$DYING" ]; then
    echo "Dying channels:"
    echo "$DYING"
fi
```

### 3.3 استخراج آخر حدث cacheex

```bash
#!/bin/bash
# آخر drop لكل نظير
echo "CACHEEX" | nc localhost 5501 | grep '| last drop:' | while read line; do
    peer=$(echo "$line" | awk '{print $1}')
    reason=$(echo "$line" | awk -F': ' '{print $2}' | awk '{print $1}')
    channel=$(echo "$line" | awk -F': ' '{print $2}' | awk '{print $2}')
    echo "Peer $peer: $reason on $channel"
done
```

---

## 4. رموز الأخطاء

### 4.1 HTTP

| الرمز | المعنى | السبب |
|---|---|---|
| 200 | OK | نجاح |
| 401 | Unauthorized | مصادقة خاطئة |
| 404 | Not Found | مسار غير موجود |
| 000 | Connection closed | صفحة stats في نكهة stock |

### 4.2 Telnet

| الرسالة | المعنى |
|---|---|
| `unknown command` | أمر غير معروف |
| `login failed` | مصادقة خاطئة |
| `access denied` | صلاحيات غير كافية |

---

## 5. الحدود والأداء

| المقياس | الحد | ملاحظات |
|---|---|---|
| **HTTP connections** | 10 متزامنة | يُرفض الزائد |
| **Telnet sessions** | 5 متزامنة | يُرفض الزائد |
| **JSON size** | ~100 KB | يعتمد على عدد البروفايلات/العملاء |
| **Response time** | <100 ms | في الظروف العادية |

---

## 6. الأمان

### 6.1 المبادئ

- **غيّر كلمات المرور الافتراضية** فور التثبيت.
- **قيّد الوصول** عبر firewall (انظر `DEPLOYMENT-ar.md`).
- **لا تُعرّض المنافذ** على الإنترنت مباشرة.
- **استخدم HTTPS** (عبر reverse proxy) إذا لزم الأمر.

### 6.2 Reverse Proxy (nginx)

```nginx
server {
    listen 443 ssl;
    server_name multics.example.com;
    
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    location / {
        proxy_pass http://127.0.0.1:5500;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        auth_basic "MultiCS";
        auth_basic_user_file /etc/nginx/.htpasswd;
    }
}
```

---

## 7. المراجع

- **[DEPLOYMENT-ar.md](DEPLOYMENT-ar.md)** — دليل النشر
- **[CONFIG-REFERENCE-ar.md](CONFIG-REFERENCE-ar.md)** — مرجع الإعداد
- **[TROUBLESHOOTING-ar.md](TROUBLESHOOTING-ar.md)** — حل المشاكل
- **[FIELD-GUIDE-ar.md](FIELD-GUIDE-ar.md)** — الدليل الميداني للمشغّل
