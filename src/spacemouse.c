/*
Copyright (c) 2013 Rolf Morel

This program is part of spacemouse-utils.

spacemouse-utils - a collection of simple utilies for 3D/6DoF input devices.

spacemouse-utils is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

spacemouse-utils is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with spacemouse-utils.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include <getopt.h>

#include <poll.h>

#include <libspacemouse.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define EXIT_ERROR 2

#define MIN_DEVIATION 256
#define N_EVENTS 16

enum {
  NO_CMD = 1,
  LIST_CMD = 1 << 1,
  LED_CMD = 1 << 2,
  EVENT_CMD = 1 << 3
};

struct axis_event {
  unsigned int n_events;
  unsigned int millis;
  char const * const event_str;
};

/* strings based on use of AXIS_MAP_SPACENAVD macro during libspacemouse
 * compilation
 */
struct axis_event axis_pos_map[] = { { 0, 0, "right" },
                                     { 0, 0, "up" },
                                     { 0, 0, "forward" },
                                     { 0, 0, "pitch back" },
                                     { 0, 0, "yaw left" },
                                     { 0, 0, "roll right" },
                                    };

struct axis_event axis_neg_map[] = { { 0, 0, "left" },
                                     { 0, 0, "down" },
                                     { 0, 0, "back" },
                                     { 0, 0, "pitch forward" },
                                     { 0, 0, "yaw right" },
                                     { 0, 0, "roll left" },
                                    };

int run_regex(char const *regex, char const *string, int comp_mask)
{
  regex_t preg;
  int ret;

  if (regcomp(&preg, regex, comp_mask) != 0)
    return -1;

  ret = regexec(&preg, string, 0, NULL, 0);

  regfree(&preg);

  return ret;
}

int main(int argc, char **argv)
{
  struct spacemouse *iter;
  spacemouse_event mouse_event;
  int command = NO_CMD, multi_call = 0;

  char const *dev_opt = NULL, *man_opt = NULL, *pro_opt = NULL;
  int regex_mask = REG_EXTENDED | REG_NOSUB;
  int grab_dev = 0, min_deviation = MIN_DEVIATION, n_events = 0;
  int millis_period = 0;

  int state_arg = -1;

  int c, monitor_fd = -1;

#define COMMON_LONG_OPTIONS \
    { "devnode", required_argument, NULL, 'd' }, \
    { "manufacturer", required_argument, NULL, 'm' }, \
    { "product", required_argument, NULL, 'p' }, \
    { "ignore-case", no_argument, NULL, 'i' }, \
    { "help", no_argument, NULL, 'h' },

  char const *opt_str_no_cmd = "d:m:p:ih";
  struct option const long_options_no_cmd[] = {
    COMMON_LONG_OPTIONS
    { 0, 0, 0, 0 }
  };

  char const *opt_str_event_cmd = "d:m:p:igD:n:M:h";
  struct option const long_options_event_cmd[] = {
    COMMON_LONG_OPTIONS
    { "grab", no_argument, NULL, 'g'},
    { "deviation", required_argument, NULL, 'D' },
    { "events", required_argument, NULL, 'n' },
    { "millis", required_argument, NULL, 'M' },
    { 0, 0, 0, 0 }
  };

  char const *opt_str = opt_str_no_cmd;
  struct option const *long_options = long_options_no_cmd;

  {
    int str_len = strlen(*argv);

    if (str_len >= 15 &&
        strcmp(*argv + (str_len - 15), "spacemouse-list") == 0) {
      multi_call = 1;
      command = LIST_CMD;
    } else if (str_len >= 14 &&
               strcmp(*argv + (str_len - 14), "spacemouse-led") == 0) {
      multi_call = 1;
      command = LED_CMD;
    } else if (str_len >= 16 &&
               strcmp(*argv + (str_len - 16), "spacemouse-event") == 0) {
      multi_call = 1;
      command = EVENT_CMD;
      opt_str = opt_str_event_cmd;
      long_options = long_options_event_cmd;
    } else if (argc >= 2) {
      if (strcmp(argv[1], "list") == 0) {
        command = LIST_CMD;
        optind = 2;
      } else if (strcmp(argv[1], "led") == 0) {
        command = LED_CMD;
        optind = 2;
      } else if (strcmp(argv[1], "event") == 0) {
        command = EVENT_CMD;
        optind = 2;
        opt_str = opt_str_event_cmd;
        long_options = long_options_event_cmd;
      }
    }

    char const help_no_cmd[] = \
"Usage: spacemouse [OPTIONS]\n"
"       spacemouse <COMMAND> [OPTIONS]\n"
"       spacemouse led [OPTIONS] (on | 1) | (off | 0)\n"
"       spacemouse led [OPTIONS] switch\n"
"       spacemouse event [OPTIONS] (--events <N> | --millis <MILLISECONDS>)\n"
"       spacemouse [-h | --help]\n"
"\n"
"Commands: (defaults to 'list' if no command is specified)\n"
"  list: Print device information of connected 3D/6DoF input devices\n"
"  led: Print or manipulate the LED state of connected 3D/6DoF input devices\n"
"  event: Print events generated by connected 3D/6DoF input devices\n"
"\n";

    char const help_list_cmd[] = \
"Usage: spacemouse-list [OPTIONS]\n"
"       spacemouse-list [-h | --help]\n"
"Print device information of connected 3D/6DoF input devices.\n"
"\n";

    char const help_led_cmd[] = \
"Usage: spacemouse-led [OPTIONS]\n"
"       spacemouse-led [OPTIONS] (on | 1) | (off | 0)\n"
"       spacemouse-led [OPTIONS] switch\n"
"       spacemouse-led [-h | --help]\n"
"Print or manipulate the LED state of connected 3D/6DoF input devices.\n"
"\n";

    char const help_event_cmd[] = \
"Usage: spacemouse-event [OPTIONS]\n"
"       spacemouse-event [OPTIONS] (--events <N> | --millis <MILLISECONDS>)\n"
"       spacemouse-event [-h | --help]\n"
"Print events generated by connected 3D/6DoF input devices.\n"
"\n";

    char const help_common_opts[] = \
"Options:\n"
"  -d, --devnode=DEV          regular expression (ERE) which devices'\n"
"                             devnode string must match\n"
"  -m, --manufacturer=MAN     regular expression (ERE) which devices'\n"
"                             manufacturer string must match\n"
"  -p, --product=PRO          regular expression (ERE) which devices'\n"
"                             product string must match\n"
"  -i, --ignore-case          makes regular expression matching case\n"
"                             insensitive\n";

    char const help_event_opts[] = \
"  -D, --deviation DEVIATION  minimum deviation on an motion axis needed\n"
"                             to register as an event\n"
"                             default is: " STR(MIN_DEVIATION) "\n"
"  -n, --events N             number of consecutive events for which\n"
"                             deviaton must exceed minimum deviation before\n"
"                             printing an event to stdout\n"
"                             default is: " STR(N_EVENTS) "\n"
"  -M, --millis MILLISECONDS  millisecond period in which consecutive\n"
"                             events' deviaton must exceed minimum deviation\n"
"                             before printing an event to stdout\n";

    char const help_common_opts_end[] = \
"  -h, --help                 display this help\n"
"\n";

    while ((c = getopt_long(argc, argv, opt_str, long_options, NULL)) != -1) {
      int tmp;
      switch (c) {
        case 'd':
          dev_opt = optarg;
          break;

        case 'm':
          man_opt = optarg;
          break;

        case 'p':
          pro_opt = optarg;
          break;

        case 'i':
          regex_mask |= REG_ICASE;
          break;

        case 'g':
          grab_dev = 1;
          break;

        case 'D':
          if ((tmp = atoi(optarg)) < 1) {
            fprintf(stderr, "%s: option '--deviation' needs to be a valid "
                    "positive integer\n", *argv);
            exit(EXIT_FAILURE);
          } else
            min_deviation = tmp;
          break;

        case 'n':
          if ((tmp = atoi(optarg)) < 1) {
            fprintf(stderr, "%s: option '--n-events' needs to be a valid "
                    "positive integer\n", *argv);
            exit(EXIT_FAILURE);
          } else
            n_events = tmp;
          break;

        case 'M':
          if ((tmp = atoi(optarg)) < 1) {
            fprintf(stderr, "%s: option '--millis' needs to be a valid "
                    "positive integer, in milliseconds\n", *argv);
            exit(EXIT_FAILURE);
          } else
            millis_period = tmp;
          break;

        case 'h':
          if (multi_call && command == LIST_CMD)
            printf("%s", help_list_cmd);
          else if (multi_call && command == LED_CMD)
            printf("%s", help_led_cmd);
          else if (multi_call && command == EVENT_CMD)
            printf("%s", help_event_cmd);
          else
            printf("%s", help_no_cmd);

          printf("%s", help_common_opts);

          if (command & (NO_CMD | EVENT_CMD))
            printf("%s", help_event_opts);

          printf("%s", help_common_opts_end);

          exit(EXIT_SUCCESS);
          break;

        case '?':
          exit(EXIT_FAILURE);
          break;
      }
    }
  }

  if (n_events != 0 && millis_period != 0) {
    fprintf(stderr, "%s: options '--n-events' and '--millis' are mutually "
            "exclusive\n", *argv);
    exit(EXIT_FAILURE);
  } else if (n_events == 0)
    n_events = N_EVENTS;

  {
    int invalid = 0;
    if (optind == (argc - 1)) {
      if (command == LED_CMD) {
        char *ptr;
        for (ptr = argv[optind]; *ptr != 0; ptr++)
          *ptr = tolower(*ptr);

        if (strcmp(argv[optind], "on") == 0 || strcmp(argv[optind], "1") == 0)
          state_arg = 1;
        else if (strcmp(argv[optind], "off") == 0 ||
                 strcmp(argv[optind], "0") == 0)
          state_arg = 0;
        else if (strcmp(argv[optind], "switch") == 0)
          state_arg = 2;
        else
          invalid = 3;
      } else if (command == NO_CMD)
        invalid = 4;
      else
        invalid = 1;
    } else if (optind != argc) {
      if (command & (LED_CMD | NO_CMD))
        invalid = 2;
      else
        invalid = 1;
    }

    if (invalid > 0) {
      if (invalid == 1)
        fprintf(stderr, "%s: does not take non-option arguments\n", *argv);
      else if (invalid == 2)
        fprintf(stderr, "%s: expected zero or one non-option arguments, see "
                "'-h' for help\n", *argv);
      else if (invalid == 3)
        fprintf(stderr, "%s: invalid non-option argument -- '%s', see '-h' "
                "for help\n", *argv, argv[optind]);
      else if (invalid == 4)
        fprintf(stderr, "%s: invalid command argument -- '%s', see '-h' for "
                "help\n", *argv, argv[optind]);

      exit(EXIT_FAILURE);
    }
  }

  if (command == EVENT_CMD)
    monitor_fd = spacemouse_monitor_open();

  spacemouse_device_list_foreach(iter, spacemouse_device_list_update()) {
    int skip = 0;
    char const *opts[] = { dev_opt, man_opt, pro_opt };
    char const *members[] = { spacemouse_device_get_devnode(iter),
                              spacemouse_device_get_manufacturer(iter),
                              spacemouse_device_get_product(iter) };

    for (int n = 0; n < 3; n++) {
      if (opts[n] != NULL) {
        int regex_success = run_regex(opts[n], members[n], regex_mask);
        if (regex_success == -1) {
          fprintf(stderr, "%s: failed to use regex for '%s' option -- %s\n",
                  *argv, long_options[n].name, opts[n]);
          exit(EXIT_FAILURE);
        } else if (regex_success == 1)
          skip++;
      }
    }

    if (skip == 0) {
      int led_state = -1;

      if (command & (NO_CMD | LIST_CMD)) {
        printf("devnode: %s\n"
               "manufacturer: %s\n"
               "product: %s\n"
               "\n",
               spacemouse_device_get_devnode(iter),
               spacemouse_device_get_manufacturer(iter),
               spacemouse_device_get_product(iter));
        fflush(stdout);

      } else if (spacemouse_device_open(iter) == -1) {
        fprintf(stderr, "%s: failed to open device: %s\n", *argv,
                spacemouse_device_get_devnode(iter));
        exit(EXIT_FAILURE);
      }

      if (grab_dev && spacemouse_device_grab(iter) != 0) {
        fprintf(stderr, "%s: failed to grab device: %s\n", *argv,
                spacemouse_device_get_devnode(iter));
        exit(EXIT_FAILURE);
      }

      if (command == LED_CMD) {
        if (state_arg == -1 || state_arg == 2) {
          led_state = spacemouse_device_get_led(iter);
          if (led_state == -1) {
            fprintf(stderr, "%s: failed to get led state for: %s\n", *argv,
                    spacemouse_device_get_devnode(iter));
            exit(EXIT_FAILURE);
          }
        }

        if (state_arg == -1) {
          printf("%s: %s\n", spacemouse_device_get_devnode(iter),
                 led_state ? "on": "off");
          fflush(stdout);
        } else {
          int state = state_arg;

          if (state_arg == 2)
            state = !led_state;
          if (spacemouse_device_set_led(iter, state) != 0) {
            fprintf(stderr, "%s: failed to set led state for: %s\n", *argv,
                    spacemouse_device_get_devnode(iter));
            exit(EXIT_FAILURE);
          }
          if (state_arg == 2) {
            printf("%s: switched %s\n", spacemouse_device_get_devnode(iter),
                   state ? "on": "off");
            fflush(stdout);
          }
        }

        spacemouse_device_close(iter);
      }
    }
  }

  if (command != EVENT_CMD)
    exit(EXIT_SUCCESS);

  while(1) {
    int mouse_fd, fds_idx = 0;

    spacemouse_device_list_foreach(iter, spacemouse_device_list())
      if (spacemouse_device_get_fd(iter) > -1)
        fds_idx++;

    struct pollfd fds[fds_idx + 1];
    fds_idx = 0;

    fds[fds_idx].fd = STDOUT_FILENO;
    fds[fds_idx++].events = POLLERR;

    fds[fds_idx].fd = monitor_fd;
    fds[fds_idx++].events = POLLIN;

    spacemouse_device_list_foreach(iter, spacemouse_device_list())
      if ((mouse_fd = spacemouse_device_get_fd(iter)) > -1) {
        fds[fds_idx].fd = mouse_fd;
        fds[fds_idx++].events = POLLIN;
      }

    poll(fds, fds_idx, -1);

    iter = spacemouse_device_list();

    for (int n = 0; n < fds_idx; n++) {
      if (fds[n].revents == 0)
        continue;

      if (fds[n].fd == STDOUT_FILENO && fds[n].revents & POLLERR) {
        exit(EXIT_ERROR);

      } else if (fds[n].fd == monitor_fd) {
        struct spacemouse *mon_mouse;
        int action;
        mon_mouse = spacemouse_monitor(&action);

        if (action == SPACEMOUSE_ACTION_ADD) {
          int skip = 0;
          char const *opts[] = { dev_opt, man_opt, pro_opt };
          char const *members[] = { spacemouse_device_get_devnode(mon_mouse),
              spacemouse_device_get_manufacturer(mon_mouse),
              spacemouse_device_get_product(mon_mouse) };

          for (int i = 0; i < 3; i++) {
            int regex_success;
            if (opts[i] != NULL) {
              regex_success = run_regex(opts[i], members[i], regex_mask);
              if (regex_success == -1) {
                fprintf(stderr,
                        "%s: failed to use regex for '%s' option -- %s\n",
                        *argv, long_options[i].name, opts[i]);
                exit(EXIT_FAILURE);
              } else if (regex_success == 0)
                skip++;
            }
          }

          if (skip == 0) {
            if (spacemouse_device_open(mon_mouse) == -1) {
              fprintf(stderr, "%s: failed to open device: %s\n", *argv,
                      spacemouse_device_get_devnode(mon_mouse));
              exit(EXIT_FAILURE);
            }

            if (grab_dev && spacemouse_device_grab(iter) != 0) {
              fprintf(stderr, "%s: failed to grab device: %s\n", *argv,
                      spacemouse_device_get_devnode(iter));
              exit(EXIT_FAILURE);
            }

            printf("device: %s %s %s connect\n",
                   spacemouse_device_get_devnode(mon_mouse),
                   spacemouse_device_get_manufacturer(mon_mouse),
                   spacemouse_device_get_product(mon_mouse));
            fflush(stdout);
          }
        } else if (action == SPACEMOUSE_ACTION_REMOVE) {
          if (spacemouse_device_get_fd(mon_mouse) > -1) {
            printf("device: %s %s %s disconnect\n",
                   spacemouse_device_get_devnode(mon_mouse),
                   spacemouse_device_get_manufacturer(mon_mouse),
                   spacemouse_device_get_product(mon_mouse));
            fflush(stdout);
          }
          spacemouse_device_close(mon_mouse);
        }

      } else {
        spacemouse_device_list_foreach(iter, iter) {
          mouse_fd = spacemouse_device_get_fd(iter);
          if (mouse_fd > -1 && fds[n].fd == mouse_fd) {
            int status;

            memset(&mouse_event, 0, sizeof mouse_event);

            status = spacemouse_device_read_event(iter, &mouse_event);
            if (status == -1)
              spacemouse_device_close(iter);
            else if (status == SPACEMOUSE_READ_SUCCESS) {
              if (mouse_event.type == SPACEMOUSE_EVENT_MOTION) {
                int *int_ptr = &mouse_event.motion.x;
                for (int i = 0; i < 6; i++) {
                  int print_event = 0;
                  struct axis_event *axis_map = 0, *axis_inverse_map;

                  if (int_ptr[i] > min_deviation) {
                    axis_map = axis_pos_map;
                    axis_inverse_map = axis_neg_map;
                  } else if (int_ptr[i] < (-1 * min_deviation)) {
                    axis_map = axis_neg_map;
                    axis_inverse_map = axis_pos_map;
                  } else {
                    axis_pos_map[i].n_events = 0;
                    axis_neg_map[i].n_events = 0;
                    axis_pos_map[i].millis = 0;
                    axis_neg_map[i].millis = 0;
                  }

                  if (axis_map != 0) {
                    if (millis_period != 0) {
                      axis_map[i].millis += mouse_event.motion.period;
                      if (axis_map[i].millis > millis_period) {
                        axis_map[i].millis = \
                            axis_map[i].millis % millis_period;
                        print_event = 1;
                      }

                    } else {
                      axis_map[i].n_events += 1;
                      axis_inverse_map[i].n_events = 0;
                      if ((axis_map[i].n_events % n_events) == 0)
                        print_event = 1;
                    }

                    if (print_event) {
                      printf("motion: %s\n", axis_map[i].event_str);
                      fflush(stdout);
                    }
                  }
                }
              } else if (mouse_event.type == SPACEMOUSE_EVENT_BUTTON) {
                printf("button: %d %s\n", mouse_event.button.bnum,
                       mouse_event.button.press ? "press" : "release");
                fflush(stdout);
              }
            }
          break;
          }
        }
      }
    }
  }

  return 0;
}
