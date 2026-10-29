#!/bin/bash
###############################################################################
# MultiCS System Monitor - أداة مراقبة شاملة
# Version: 1.0
# Date: 2026-10-26
# 
# DESCRIPTION:
#   مراقبة شاملة لجميع الأنظمة المُفعّلة في MultiCS:
#   - Black Screen Prevention
#   - R200 v2.5 (SuperSmartFilter + DCW STRICT)
#   - Provider Optimizations
#   - Cache Efficiency
#   - Fake CW Detection
#
# USAGE:
#   ./scripts/monitor-systems.sh [option]
#
# OPTIONS:
#   --all        عرض جميع الإحصائيات (افتراضي)
#   --bs         مراقبة Black Screen Prevention فقط
#   --ssf        مراقبة SuperSmartFilter فقط
#   --dcw        مراقبة DCW STRICT فقط
#   --cache      مراقبة Cache فقط
#   --fake       مراقبة Fake CW Detection فقط
#   --live       مراقبة حية (تحديث كل 2 ثانية)
#   --help       عرض المساعدة
#
###############################################################################

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Default log file
LOG_FILE="${MULTICS_LOG:-/tmp/multics.log}"

# Check if log file exists
check_log_file() {
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${RED}❌ Error: Log file not found: $LOG_FILE${NC}"
        echo -e "${YELLOW}💡 Set MULTICS_LOG environment variable or ensure MultiCS is running${NC}"
        exit 1
    fi
}

# Print header
print_header() {
    echo -e "${CYAN}"
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║         MultiCS System Monitor - مراقبة شاملة                ║"
    echo "║                    Version 1.0 (2026-10-26)                  ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# Print section header
print_section() {
    local title="$1"
    local color="${2:-$BLUE}"
    echo -e "\n${color}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${color}📊 $title${NC}"
    echo -e "${color}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
}

# Count occurrences in log
count_log() {
    local pattern="$1"
    local time_range="${2:-24h}"
    
    if [ "$time_range" = "24h" ]; then
        grep -c "$pattern" "$LOG_FILE" 2>/dev/null || echo "0"
    elif [ "$time_range" = "1h" ]; then
        grep "$pattern" "$LOG_FILE" 2>/dev/null | tail -n $(grep -c "$(date '+%H')" "$LOG_FILE" 2>/dev/null || echo "0") | wc -l
    else
        grep -c "$pattern" "$LOG_FILE" 2>/dev/null || echo "0"
    fi
}

# Monitor Black Screen Prevention
monitor_black_screen() {
    print_section "Black Screen Prevention - منع البلاك سكرين" "$RED"
    
    local cycle_mismatch=$(count_log "CYCLE MISMATCH\|cycle mismatch")
    local cycle_ok=$(count_log "CYCLE OK\|cycle validated")
    local bs_detected=$(count_log "BLACK SCREEN\|BS_DETECT")
    local bs_prevented=$(count_log "BS_PREVENT\|Black Screen Prevented")
    
    echo -e "${YELLOW}📈 Cycle Validation:${NC}"
    echo -e "  ✅ Cycle OK:        ${GREEN}$cycle_ok${NC}"
    echo -e "  ⚠️  Cycle Mismatch:  ${YELLOW}$cycle_mismatch${NC}"
    echo ""
    echo -e "${YELLOW}🛡️  Black Screen Protection:${NC}"
    echo -e "  🔴 BS Detected:     ${RED}$bs_detected${NC}"
    echo -e "  ✅ BS Prevented:    ${GREEN}$bs_prevented${NC}"
    
    if [ "$cycle_mismatch" -gt 0 ]; then
        echo -e "\n${YELLOW}💡 Recent Cycle Mismatches:${NC}"
        grep -i "cycle mismatch" "$LOG_FILE" 2>/dev/null | tail -5 | sed 's/^/  /'
    fi
}

# Monitor SuperSmartFilter (SSF)
monitor_ssf() {
    print_section "R200 v2.5 SuperSmartFilter - الفلتر الذكي" "$MAGENTA"
    
    local ssf_checks=$(count_log "SSF_CHECK\|supersmartfilter_check")
    local ssf_accept=$(count_log "SSF_ACCEPT\|SSF.*accepted")
    local ssf_reject=$(count_log "SSF_REJECT\|SSF.*rejected")
    local ssf_fake=$(count_log "SSF.*FAKE\|SSF.*fake detected")
    local ssf_entropy=$(count_log "SSF.*ENTROPY\|SSF.*entropy")
    local ssf_pattern=$(count_log "SSF.*PATTERN\|SSF.*pattern")
    local ssf_blacklist=$(count_log "SSF.*BLACKLIST\|SSF.*blacklist")
    local ssf_banned=$(count_log "PEER_BANNED\|peer banned")
    
    echo -e "${YELLOW}📊 SSF Statistics:${NC}"
    echo -e "  🔍 Total Checks:    ${CYAN}$ssf_checks${NC}"
    echo -e "  ✅ Accepted:        ${GREEN}$ssf_accept${NC}"
    echo -e "  ❌ Rejected:        ${RED}$ssf_reject${NC}"
    echo ""
    echo -e "${YELLOW}🎯 Rejection Reasons:${NC}"
    echo -e "  🎭 Fake CW:         ${RED}$ssf_fake${NC}"
    echo -e "  📉 Low Entropy:     ${YELLOW}$ssf_entropy${NC}"
    echo -e "  🔢 Pattern Match:   ${YELLOW}$ssf_pattern${NC}"
    echo -e "  ⛔ Blacklisted:     ${RED}$ssf_blacklist${NC}"
    echo ""
    echo -e "${YELLOW}🚫 Peer Management:${NC}"
    echo -e "  🚷 Peers Banned:    ${RED}$ssf_banned${NC}"
    
    if [ "$ssf_reject" -gt 0 ]; then
        local reject_rate=$((ssf_reject * 100 / (ssf_checks > 0 ? ssf_checks : 1)))
        echo -e "\n${CYAN}📊 Rejection Rate: ${MAGENTA}${reject_rate}%${NC}"
    fi
}

# Monitor DCW STRICT
monitor_dcw_strict() {
    print_section "R200 v2.5 DCW STRICT - فحص صارم" "$BLUE"
    
    local dcw_checks=$(count_log "DCW_STRICT\|dcwstrict_check")
    local dcw_accept=$(count_log "DCW_STRICT.*ACCEPT\|DCW.*accepted")
    local dcw_reject=$(count_log "DCW_STRICT.*REJECT\|DCW.*rejected")
    local dcw_entropy=$(count_log "DCW.*ENTROPY\|DCW.*entropy")
    local dcw_same_byte=$(count_log "DCW.*SAME_BYTE\|DCW.*all-same")
    local dcw_stale=$(count_log "DCW.*STALE\|DCW.*stale")
    
    echo -e "${YELLOW}📊 DCW STRICT Statistics:${NC}"
    echo -e "  🔍 Total Checks:    ${CYAN}$dcw_checks${NC}"
    echo -e "  ✅ Accepted:        ${GREEN}$dcw_accept${NC}"
    echo -e "  ❌ Rejected:        ${RED}$dcw_reject${NC}"
    echo ""
    echo -e "${YELLOW}🎯 Rejection Reasons:${NC}"
    echo -e "  📉 Low Entropy:     ${YELLOW}$dcw_entropy${NC}"
    echo -e "  🔢 All-Same-Byte:   ${YELLOW}$dcw_same_byte${NC}"
    echo -e "  ⏰ Stale CW (>5s):  ${YELLOW}$dcw_stale${NC}"
    
    if [ "$dcw_reject" -gt 0 ]; then
        local reject_rate=$((dcw_reject * 100 / (dcw_checks > 0 ? dcw_checks : 1)))
        echo -e "\n${CYAN}📊 Rejection Rate: ${BLUE}${reject_rate}%${NC}"
    fi
}

# Monitor Cache
monitor_cache() {
    print_section "Cache Efficiency - كفاءة الكاش" "$GREEN"
    
    local cache_hits=$(count_log "CACHE HIT\|cache hit\|CACHE_HIT")
    local cache_miss=$(count_log "CACHE MISS\|cache miss\|CACHE_MISS")
    local cache_ex=$(count_log "CACHEEX\|cache exchange")
    local cache_timeout=$(count_log "CACHE TIMEOUT\|cache timeout")
    
    echo -e "${YELLOW}📊 Cache Statistics:${NC}"
    echo -e "  ✅ Cache Hits:      ${GREEN}$cache_hits${NC}"
    echo -e "  ❌ Cache Misses:    ${YELLOW}$cache_miss${NC}"
    echo -e "  🔄 Cache Exchange:  ${CYAN}$cache_ex${NC}"
    echo -e "  ⏰ Timeouts:        ${RED}$cache_timeout${NC}"
    
    if [ "$cache_hits" -gt 0 ] || [ "$cache_miss" -gt 0 ]; then
        local total=$((cache_hits + cache_miss))
        local hit_rate=$((cache_hits * 100 / (total > 0 ? total : 1)))
        echo -e "\n${CYAN}📊 Cache Hit Rate: ${GREEN}${hit_rate}%${NC}"
        
        if [ "$hit_rate" -ge 80 ]; then
            echo -e "${GREEN}✅ Excellent cache performance!${NC}"
        elif [ "$hit_rate" -ge 60 ]; then
            echo -e "${YELLOW}⚠️  Good cache performance${NC}"
        else
            echo -e "${RED}⚠️  Cache performance needs improvement${NC}"
        fi
    fi
}

# Monitor Fake CW Detection
monitor_fake_cw() {
    print_section "Fake CW Detection - كشف المفاتيح المزيفة" "$RED"
    
    local fake_detected=$(count_log "FAKE CW\|fake.*detected\|FAKE_CW")
    local fake_pattern=$(count_log "dcw_pattern_fake\|PATTERN FAKE")
    local fake_rejected=$(count_log "fake.*rejected\|FAKE.*REJECT")
    
    echo -e "${YELLOW}📊 Fake CW Statistics:${NC}"
    echo -e "  🔍 Fake Detected:   ${RED}$fake_detected${NC}"
    echo -e "  🔢 Pattern Match:   ${YELLOW}$fake_pattern${NC}"
    echo -e "  ❌ Rejected:        ${RED}$fake_rejected${NC}"
    
    if [ "$fake_detected" -gt 0 ]; then
        echo -e "\n${YELLOW}💡 Recent Fake CWs:${NC}"
        grep -i "fake" "$LOG_FILE" 2>/dev/null | tail -5 | sed 's/^/  /'
    fi
}

# Monitor Provider Optimizations
monitor_providers() {
    print_section "Provider Optimizations - تحسينات المزودين" "$CYAN"
    
    local crc_disabled=$(count_log "CRC.*disabled\|disablecrccws")
    local whitelist=$(count_log "WHITELIST\|ecmwhitelist")
    local cycle_check=$(count_log "CYCLE_CHECK\|cycle check")
    local force_even=$(count_log "FORCE_EVEN\|force_even_sa")
    local block_emm=$(count_log "BLOCK_EMM\|block.*emm")
    
    echo -e "${YELLOW}📊 Provider Features:${NC}"
    echo -e "  🔧 CRC Disabled:    ${CYAN}$crc_disabled${NC}"
    echo -e "  ✅ Whitelist:       ${GREEN}$whitelist${NC}"
    echo -e "  🔄 Cycle Check:     ${YELLOW}$cycle_check${NC}"
    echo -e "  ⚡ Force Even SA:   ${MAGENTA}$force_even${NC}"
    echo -e "  🚫 Block EMM:       ${RED}$block_emm${NC}"
}

# Show all statistics
show_all() {
    print_header
    check_log_file
    
    echo -e "${GREEN}📁 Log File: ${CYAN}$LOG_FILE${NC}"
    echo -e "${GREEN}📅 Date: ${CYAN}$(date '+%Y-%m-%d %H:%M:%S')${NC}"
    
    monitor_black_screen
    monitor_ssf
    monitor_dcw_strict
    monitor_cache
    monitor_fake_cw
    monitor_providers
    
    echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}✅ Monitoring complete!${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
}

# Live monitoring
live_monitor() {
    print_header
    check_log_file
    
    echo -e "${YELLOW}🔴 Live Monitoring - Press Ctrl+C to stop${NC}\n"
    
    while true; do
        clear
        print_header
        echo -e "${GREEN}📁 Log File: ${CYAN}$LOG_FILE${NC}"
        echo -e "${GREEN}📅 Time: ${CYAN}$(date '+%Y-%m-%d %H:%M:%S')${NC}"
        echo -e "${YELLOW}🔄 Refreshing every 2 seconds...${NC}"
        
        monitor_black_screen
        monitor_ssf
        monitor_dcw_strict
        monitor_cache
        
        sleep 2
    done
}

# Show help
show_help() {
    echo "MultiCS System Monitor - أداة مراقبة شاملة"
    echo ""
    echo "USAGE:"
    echo "  ./scripts/monitor-systems.sh [option]"
    echo ""
    echo "OPTIONS:"
    echo "  --all        عرض جميع الإحصائيات (افتراضي)"
    echo "  --bs         مراقبة Black Screen Prevention فقط"
    echo "  --ssf        مراقبة SuperSmartFilter فقط"
    echo "  --dcw        مراقبة DCW STRICT فقط"
    echo "  --cache      مراقبة Cache فقط"
    echo "  --fake       مراقبة Fake CW Detection فقط"
    echo "  --providers  مراقبة Provider Optimizations فقط"
    echo "  --live       مراقبة حية (تحديث كل 2 ثانية)"
    echo "  --help       عرض المساعدة"
    echo ""
    echo "ENVIRONMENT:"
    echo "  MULTICS_LOG  مسار ملف اللوج (افتراضي: /tmp/multics.log)"
    echo ""
    echo "EXAMPLES:"
    echo "  ./scripts/monitor-systems.sh"
    echo "  ./scripts/monitor-systems.sh --live"
    echo "  ./scripts/monitor-systems.sh --ssf"
    echo "  MULTICS_LOG=/var/log/multics.log ./scripts/monitor-systems.sh"
}

# Main
case "${1:---all}" in
    --all)
        show_all
        ;;
    --bs)
        print_header
        check_log_file
        monitor_black_screen
        ;;
    --ssf)
        print_header
        check_log_file
        monitor_ssf
        ;;
    --dcw)
        print_header
        check_log_file
        monitor_dcw_strict
        ;;
    --cache)
        print_header
        check_log_file
        monitor_cache
        ;;
    --fake)
        print_header
        check_log_file
        monitor_fake_cw
        ;;
    --providers)
        print_header
        check_log_file
        monitor_providers
        ;;
    --live)
        live_monitor
        ;;
    --help|-h)
        show_help
        ;;
    *)
        echo -e "${RED}❌ Unknown option: $1${NC}"
        show_help
        exit 1
        ;;
esac
