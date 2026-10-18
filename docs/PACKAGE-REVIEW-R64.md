# مراجعة الباقات لكل بروفايل — مُولَّد (R64)

مُولَّد بـ `python3 tests/package_review.py multics-r24-optimized.cfg`. لا يُعدِّل الملف، ومعناه «قِس قبل أن تغيّر»، لا «غيّر الآن». الشرح في `docs/PACKAGE-PROFILES-R64-ar.md`.

# package review: multics-r24-optimized.cfg
profiles: 72

| profile | CAID | CA system (code) | CW format | providers | checksum gate | ECM LEN | flags |
|---|---|---|---|---|---|---|---|
| Vivacom - 09BD | 09BD | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Focus - 0B02 | 0B02 | Conax | CSA | 000000 | on (CSA checksum CW) | YES | - |
| YES+AMOS - 090D | 090D | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Orange SK - 0609 | 0609 | Irdeto | CSA | 000000 | on (CSA checksum CW) | YES | - |
| DigitalALB - 0B00 | 0B00 | Conax | CSA | 000000 | on (CSA checksum CW) | NO | - |
| HD+ 1830 | 1830 | Nagravision | CSA | 000000,003411,008011 | on (CSA checksum CW) | NO | - |
| SAT BG - 0B01 | 0B01 | Conax | CSA | 000000 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| TOTAL TV - 091F | 091F | VideoGuard | CSA | 000000 | on (CSA checksum CW) | NO | - |
| C+FR - 1883 | 1883 | Nagravision | CSA | 003311 | on (CSA checksum CW) | NO | - |
| DolceTV - 092F | 092F | VideoGuard | CSA | 000000 | on (CSA checksum CW) | NO | - |
| NOS SGPS - 1802 | 1802 | Nagravision | CSA | 000000,002011,002111 | on (CSA checksum CW) | NO | - |
| CSAT NG - 1811 | 1811 | Nagravision | CSA | 003311 | on (CSA checksum CW) | NO | CRC-off-elsewhere |
| COSMOTE - 09BE | 09BE | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| UPC - 1815 | 1815 | Nagravision | CSA | 000000 | on (CSA checksum CW) | NO | - |
| Bulsatcom - 4AEE | 4AEE | DRE-Crypt | CSA | 000000,000001,000100,000300 | on (CSA checksum CW) | YES | - |
| Sky DE - 098D | 098D | VideoGuard iCam (Sky DE) | AES | 000000 | off (CA MODERN, AES carries no checksum) | NO | - |
| RCS/DIGI TV - 1880 | 1880 | Nagravision | CSA | 000000,002011,002111 | on (CSA checksum CW) | NO | - |
| OSN - 1708 | 1708 | BetaCrypt | CSA | 000000 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| DIGI TV - 1802 | 1802 | Nagravision | CSA | 000000,002011,002111 | on (CSA checksum CW) | NO | - |
| AB SAT - 1819 | 1819 | Nagravision | CSA | 000000,00006D | on (CSA checksum CW) | YES | - |
| AUSTRIASAT - 0D95 | 0D95 | CryptoWorks | CSA | 000004,000010 | on (CSA checksum CW) | YES | - |
| POLSAT - 1803/1861 | 1803 | Nagravision | CSA | 000000,007001,007101 | on (CSA checksum CW) | NO | - |
| FRA - 32830 | 0500 | Viaccess | CSA | 032830 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| BIS TV - 042820 | 0500 | Viaccess | CSA | 042820 | on (CSA checksum CW) | YES | - |
| ART - 0604 | 0604 | Irdeto | CSA | 000000 | on (CSA checksum CW) | YES | - |
| MovieStar - 1810 | 1810 | Nagravision | CSA | 000000,004001,004101 | on (CSA checksum CW) | NO | - |
| NC+ CYFRA - 0100/68 | 0100 | Seca Mediaguard | CSA | 000068 | on (CSA checksum CW) | YES | - |
| ORF - 0D98 | 0D98 | CryptoWorks | CSA | 000004,000010 | on (CSA checksum CW) | NO | - |
| NC+ 4K - 1884 | 1884 | Nagravision | CSA | 000000,002011,002111 | on (CSA checksum CW) | NO | CRC-off-elsewhere |
| SRG Swiss + Viaccess - 0500 | 0500 | Viaccess | CSA | 023800,040810,040820,050800,050810,060200 | on (CSA checksum CW) | YES | - |
| Skylink - 0D96 | 0D96 | CryptoWorks | CSA | 000000,000004,000008,00000C,000010,000048 | on (CSA checksum CW) | YES | - |
| TNTSAT - 030B00 | 0500 | Viaccess | CSA | 000000,030B00 | on (CSA checksum CW) | NO | CRC-off-elsewhere |
| MEO PT - 1814 | 1814 | Nagravision | CSA | 000000,005211 | on (CSA checksum CW) | YES | - |
| HD+ - 1843 | 1843 | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Eleven - 0100/6A+6C | 0100 | Seca Mediaguard | CSA | 00006A,00006C | on (CSA checksum CW) | NO | - |
| Movistar+ - 004106 | 0100 | Seca Mediaguard | CSA | 005001,004106,004108 | on (CSA checksum CW) | YES | - |
| SkyUK - 0963 | 0963 | VideoGuard iCam (Sky UK) | AES | 000000 | off (CA MODERN, AES carries no checksum) | YES | - |
| MEO Portugal - 0100/5221 | 0100 | Seca Mediaguard | CSA | 005221 | on (CSA checksum CW) | YES | - |
| Mediaset DTT - 1805 | 1805 | Nagravision | CSA | 000000 | on (CSA checksum CW) | NO | - |
| TivuSat - 183D | 183D | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| 0D97 - 0600 | 0D97 | CryptoWorks | CSA | 000004 | on (CSA checksum CW) | YES | - |
| Kabel NDS - 098E | 098E | VideoGuard iCam | AES | 000000 | off (CA MODERN, AES carries no checksum) | NO | - |
| Tring Digital - 0BAA | 0BAA | Conax | CSA | 000000 | on (CSA checksum CW) | NO | - |
| Viasat - 093E | 093E | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Viasat - 090F | 090F | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Vision Ukraine - 0500 | 0500 | Viaccess | CSA | 041200 | on (CSA checksum CW) | YES | - |
| XTRA TV - 0B0F | 0B0F | Conax | CSA | 000000 | on (CSA checksum CW) | YES | - |
| TVSat - 183E | 183E | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Telesat - 00006D | 0100 | Seca Mediaguard | CSA | 00006D | on (CSA checksum CW) | YES | - |
| MEO Portugal - 5221/5211/5225 | 0100 | Seca Mediaguard | CSA | 005211,005221,005225 | on (CSA checksum CW) | YES | - |
| SRG/SRF - 0500/508 | 0500 | Viaccess | CSA | 050800 | on (CSA checksum CW) | YES | - |
| HD+ 4K - 1860 | 1860 | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Sky UK - 0960 | 0960 | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Virgin Cable UK - 1841 | 1841 | Nagravision | CSA | 000000,005B01,005A01,005B02,005B03 | on (CSA checksum CW) | YES | - |
| Love Natural 4K - 0500 | 0500 | Viaccess | CSA | 051A00,023A10,051A40,051A10 | on (CSA checksum CW) | YES | - |
| D-SMART - 092B | 092B | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| Hayat - 0500 | 0500 | Viaccess | CSA | 022E00,022E10,051700 | on (CSA checksum CW) | YES | - |
| Pink - 0629 | 0629 | Irdeto | CSA | 000000 | on (CSA checksum CW) | YES | - |
| HRT - 0500/16E | 0500 | Viaccess | CSA | 020A00,020A10,020A20,020A30,020A40,020810,020910,022030,022050,030A00,030A10 | on (CSA checksum CW) | YES | - |
| Ziggo - 0606 | 0606 | Irdeto | CSA | 000000,000001,000002,000003,000004,000005,000006 | on (CSA checksum CW) | YES | - |
| Canal Digital 23.5E - 1818 | 1818 | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Canal Digital 23.5E - 1817 | 1817 | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| SCT + Penthouse HD - 0500 | 0500 | Viaccess | CSA | 050F00 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| Albanicasat - 0606 | 0606 | Irdeto | CSA | 000000 | on (CSA checksum CW) | YES | - |
| YES 4W - 0930 | 0930 | VideoGuard | CSA | 000000 | on (CSA checksum CW) | YES | - |
| France Sat - 0500 | 0500 | Viaccess | CSA | 041950,0419A0 | on (CSA checksum CW) | YES | - |
| Mediaset DTT - 1840 | 1840 | Nagravision | CSA | 000000 | on (CSA checksum CW) | NO | - |
| NC+ - 1813 | 1813 | Nagravision | CSA | 000068 | on (CSA checksum CW) | YES | - |
| C+NL Nagra - 1817 | 1817 | Nagravision | CSA | 000000 | on (CSA checksum CW) | YES | - |
| Redlight - 0500 | 0500 | Viaccess | CSA | 043800,050F00 | on (CSA checksum CW) | YES | CRC-off-elsewhere |
| Profile_BISS | 2600 | BISS1 | CONST | 000000 | off (CA MODERN, CONST carries no checksum) | YES | - |
| Profile_Tandberg | 1010 | Unknown | UNKNOWN | 000000 | on (UNKNOWN checksum CW) | YES | - |

duplicate (CAID, provider) pairs: 8
  0100:005221 -> MEO Portugal - 0100/5221 | MEO Portugal - 5221/5211/5225
  0500:050800 -> SRG Swiss + Viaccess - 0500 | SRG/SRF - 0500/508
  0500:050F00 -> SCT + Penthouse HD - 0500 | Redlight - 0500
  0606:000000 -> Ziggo - 0606 | Albanicasat - 0606
  1802:000000 -> NOS SGPS - 1802 | DIGI TV - 1802
  1802:002011 -> NOS SGPS - 1802 | DIGI TV - 1802
  1802:002111 -> NOS SGPS - 1802 | DIGI TV - 1802
  1817:000000 -> Canal Digital 23.5E - 1817 | C+NL Nagra - 1817

flag counts:
  checksum enforced where community turns it off: 9
