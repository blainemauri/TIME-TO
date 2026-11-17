#include "tt_widget.h"

TTWidgetStyle tt_widget_default_style(void) {
  return (TTWidgetStyle){
      .text = GColorBlack,
      .accent = PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack),
      .time_font = NULL,
      .name_font = NULL,
      .row_height = 0,
      .relative = false,
      .empty = NULL,
  };
}

static int16_t prv_row_height(const TTWidgetStyle *style) {
  return (style && style->row_height) ? style->row_height : 22;
}

int16_t tt_widget_height(int rows, const TTWidgetStyle *style) {
  return (int16_t)(rows * prv_row_height(style));
}

static void prv_clock(const struct tm *tm, char *buf, size_t cap) {
  if (clock_is_24h_style()) {
    snprintf(buf, cap, "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    int h = tm->tm_hour % 12;
    snprintf(buf, cap, "%d:%02d%s", h ? h : 12, tm->tm_min, tm->tm_hour < 12 ? "a" : "p");
  }
}

void tt_widget_format_when(time_t when, char *buf, size_t cap) {
  static const char *const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t now = time(NULL);
  struct tm tm_now = *localtime(&now);
  struct tm tm = *localtime(&when);
  char clock[10];
  prv_clock(&tm, clock, sizeof(clock));
  const bool same_day = tm.tm_year == tm_now.tm_year && tm.tm_yday == tm_now.tm_yday;
  if (same_day) {
    snprintf(buf, cap, "%s", clock);
  } else if (when - now < 6 * 86400 && when > now) {
    snprintf(buf, cap, "%s %s", days[tm.tm_wday % 7], clock);
  } else {
    snprintf(buf, cap, "%d %s", tm.tm_mday, months[tm.tm_mon % 12]);
  }
}

void tt_widget_format_until(time_t when, char *buf, size_t cap) {
  long seconds = (long)(when - time(NULL));
  if (seconds <= 0) {
    snprintf(buf, cap, "now");
    return;
  }
  long minutes = (seconds + 59) / 60;  // "1m" until it is actually due
  long hours = minutes / 60;
  long days = hours / 24;
  if (days >= 2) {
    snprintf(buf, cap, "%ldd", days);
  } else if (hours >= 1) {
    if (minutes % 60 && hours < 10) {
      snprintf(buf, cap, "%ldh %ldm", hours, minutes % 60);
    } else {
      snprintf(buf, cap, "%ldh", hours);
    }
  } else {
    snprintf(buf, cap, "%ldm", minutes);
  }
}

void tt_widget_draw(GContext *ctx, GRect bounds, const TTWidgetItem *items, int count,
                    int max_rows, const TTWidgetStyle *style) {
  TTWidgetStyle fallback = tt_widget_default_style();
  if (!style) {
    style = &fallback;
  }
  GFont time_font = style->time_font ? style->time_font
                                     : fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont name_font = style->name_font ? style->name_font
                                     : fonts_get_system_font(FONT_KEY_GOTHIC_18);
  const int16_t row_h = prv_row_height(style);
  const time_t now = time(NULL);
  int16_t y = bounds.origin.y;
  int drawn = 0;

  for (int i = 0; i < count && drawn < max_rows; i++) {
    if (items[i].when == 0 || items[i].when < now - 60) {
      continue;  // unused, or already in the past
    }
    if (y + row_h > bounds.origin.y + bounds.size.h) {
      break;
    }
    char when[20];
    if (style->relative && drawn == 0) {
      tt_widget_format_until(items[i].when, when, sizeof(when));
    } else {
      tt_widget_format_when(items[i].when, when, sizeof(when));
    }
    GSize when_size = graphics_text_layout_get_content_size(
        when, time_font, GRect(0, 0, bounds.size.w, row_h + 6), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft);
    graphics_context_set_text_color(ctx, style->accent);
    graphics_draw_text(ctx, when, time_font,
                       GRect(bounds.origin.x, y - 3, when_size.w + 2, row_h + 6),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    const int16_t name_x = bounds.origin.x + when_size.w + 7;
    const char *name = items[i].name[0] ? items[i].name : "Alarm";
    char label[TT_WIDGET_NAME_MAX + 4];
    snprintf(label, sizeof(label), "%s%s", items[i].snoozed ? "z " : "", name);
    graphics_context_set_text_color(ctx, style->text);
    graphics_draw_text(ctx, label, name_font,
                       GRect(name_x, y - 3, bounds.origin.x + bounds.size.w - name_x, row_h + 6),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += row_h;
    drawn++;
  }

  if (drawn == 0) {
    graphics_context_set_text_color(ctx, style->text);
    graphics_draw_text(ctx, style->empty ? style->empty : "No alarms", name_font,
                       GRect(bounds.origin.x, bounds.origin.y - 3, bounds.size.w, row_h + 6),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
