/*
 * Copyright (c) 2020 Ithai Levi
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */
#ifndef __COA$TGUARD_H__
#define __COA$TGUARD_H__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void
$output_info(const char* fmt, ...);
void
$output_error(const char* fmt, ...);
void
$output_debug(const char* fmt, ...);

#ifdef NDEBUG
#define $log_debug(M, ...)
#else
#define $log_debug(M, ...)                                                     \
    $output_info("[INFO] (%s:%d:) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#define $log_info(M, ...)                                                      \
    $output_info("[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define $log_error(M, ...)                                                     \
    $output_error("[ERROR] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

typedef struct
{
    int         code;
    const char* message;
} $status;

#define $typedef(T)                                                            \
    typedef struct                                                             \
    {                                                                          \
        T       v;                                                             \
        $status status;                                                        \
    }

#define $iserror(S) (S.code != 0)
#define $isokay(S) (S.code == 0)
#define $isvalid(V) (V.status.code == 0)
#define $ifvalid(V) if (V.status.code == 0)

#define $onerror(V) if (V.status.code != 0)
#define $onerror2(V) if (V.code != 0)

#define get_2rd_arg(arg1, arg2, arg3, ...) arg3
#define get_3rd_arg(arg1, arg2, arg3, arg4, ...) arg4
#define get_4th_arg(arg1, arg2, arg3, arg4, arg5, ...) arg5

#define $check1(C)                                                             \
    {                                                                          \
        if (!(C)) {                                                            \
            $log_error("%s", #C);                                              \
            goto error;                                                        \
        }                                                                      \
    }

#define $check2(C, E)                                                          \
    {                                                                          \
        if (!(C)) {                                                            \
            $log_error("%s", #C);                                              \
            goto E;                                                            \
        }                                                                      \
    }

#define $check3(C, M, E)                                                       \
    {                                                                          \
        if (!(C)) {                                                            \
            $log_error("%s,%s", #C, M);                                        \
            goto E;                                                            \
        }                                                                      \
    }

#define $check4(C, S, M, E)                                                    \
    {                                                                          \
        if (!(C)) {                                                            \
            $log_error("%s", M);                                               \
            S = ($status){ .code = -1, .message = M };                        \
            goto E;                                                            \
        }                                                                      \
    }

#define $certify(C, S)                                                         \
    {                                                                          \
        if (C) {                                                               \
            S = ($status){ .code = 0, .message = #C };                         \
        } else {                                                               \
            S = ($status){ .code = -1, .message = #C };                        \
        }                                                                      \
    }

#define $check_args_router(...)                                                \
    get_4th_arg(__VA_ARGS__, $check4, $check3, $check2, $check1)
#define $check(...) $check_args_router(__VA_ARGS__)(__VA_ARGS__)

#define $unwrap1(V)                                                            \
    V.v = V.v;                                                                 \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            $log_error(#V " verification failed: %s\n", v.message);            \
            goto error;                                                        \
        }                                                                      \
    }

#define $unwrap2(V, E)                                                         \
    V.v = V.v;                                                                 \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            $log_error(#V " verification failed: %s\n", v.message);            \
            goto E;                                                            \
        }                                                                      \
    }

#define $unwrap3(V, S, E)                                                      \
    V.v = V.v;                                                                 \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            $log_error(#V " verification failed: %s\n", v.message);            \
            S = ($status){ .code = -1, .message = "" };                        \
            goto E;                                                            \
        }                                                                      \
    }

#define $unwrap_args_router(...)                                               \
    get_3rd_arg(__VA_ARGS__, $unwrap3, $unwrap2, $unwrap1)
#define $unwrap(...) $unwrap_args_router(__VA_ARGS__)(__VA_ARGS__)

#define $inspect1(V)                                                           \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            printf(#V " verification failed: %s\n", v.message);                \
            goto error;                                                        \
        }                                                                      \
    }

#define $inspect2(V, E)                                                        \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            printf(#V " verification failed: %s\n", v.message);                \
            goto E;                                                            \
        }                                                                      \
    }

#define $inspect3(V, S, E)                                                     \
    {                                                                          \
        $status v = (V).status;                                                \
        if (v.code != 0) {                                                     \
            printf(#V " verification failed: %s\n", v.message);                \
            S = ($status){ .code = -1, .message = M };                         \
            goto E;                                                            \
        }                                                                      \
    }

#define $inspect_args_router(...)                                              \
    get_3rd_arg(__VA_ARGS__, $inspect3, $inspect2, $inspect1)
#define $inspect(...) $inspect_args_router(__VA_ARGS__)(__VA_ARGS__)

#define $okay ($status){ .code = 0, .message = "ok" };
#define $error(M) ($status){ .code = -1, .message = M };

/*
#define $ok ($status){ .code = 0, .message = "ok" };
#define $bad ($status){ .code = -1, .message = "bad value" };
*/

#define $invalid1(T)                                                           \
    (T) { .status = status_error("Invalid value") }
#define $invalid2(T, M)                                                        \
    (T) { .status = status_error(M) }
#define $invalid_args_router(...) get_2rd_arg(__VA_ARGS__, $invalid2, $invalid1)
#define $invalid(...) $invalid_args_router(__VA_ARGS__)(__VA_ARGS__)

static $status

status_okay()
{
    return ($status){ .code = 0, .message = "okay" };
}

static $status
status_error(const char* message)
{
    return ($status){ .code = -1, .message = message };
}

/*

$typedef(long long) t_meter;
$typedef(long long) t_second;
$typedef(long long) t_meters_per_second;


static int calc_speed(int last_distance,
               int last_time,
               int curr_distance,
               int curr_time) {
    if ((curr_time - last_time) == 0) {
        return 0;
    } else
        return (curr_distance-last_distance)/(curr_time-last_time);
}


static t_meters_per_second safe_calc_speed(
        const t_meter last_distance,
        const t_second last_time,
        const t_meter curr_distance,
        const t_second curr_time)
{
    $verify(last_distance, error);
    $verify(last_time, error);
    $verify(curr_distance, error);
    $verify(curr_time, error);
    return
        (t_meters_per_second){(curr_distance.v-last_distance.v)/(curr_time.v-last_time.v)};
error:
    return $invalid(t_meters_per_second);
}



static int xmain() {
    long long zz = 1000000000;
    int speed = calc_speed(2000,10,zz,13);
    printf("speed is: %d meters per second\n", speed);

    t_meters_per_second sspeed = safe_calc_speed(
            (t_meter){2000},
            (t_second){10},
            (t_meter){1000000000},
            (t_second){13});

    printf("sspeed is: %d meters per second\n", sspeed.v);



    return 0;
}
*/
#endif
