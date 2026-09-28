"""Summarize schema-1 performance logs; Python standard library only.
Usage: python tools/analyze_profile.py SESSION --skip-seconds 5 [--baseline OTHER]
The game and this script are never launched automatically by the coding agent.
"""
import argparse
import collections
import csv
import math
from pathlib import Path

METRICS = (
    "frame_ms", "draw_calls", "draw_cpu_ms", "swap_ms", "update_ms_total",
    "scene_sort_ms", "scene_render_ms", "navigation_ms", "world_stream_ms",
    "collision_refresh_ms", "cache_read_ms", "cache_write_ms", "cache_upload_ms",
    "mesh_upload_bytes", "instance_upload_bytes", "triangle_instances",
    "previous_log_cpu_ms",
)
OPTIONAL_METRICS = ("depth_prepare_ms", "navigation_snapshot_ms")
TOTALS = (
    "mesh_new", "mesh_loaded", "cache_hits", "cache_misses", "file_invalid",
    "file_errors", "file_saved", "file_skipped", "atlas_compactions",
    "upload_rejected", "gpu_skipped", "dt_clamped_updates",
)
CONFIG = ("width", "height", "seed", "zoom", "post_enabled", "bloom", "edge_blur",
          "vignette", "bloom_strength", "edge_strength", "paused", "scan", "foreground")


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        for line, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"{path.name}:{line}: incomplete CSV row; exit the game before analysis")
            yield row


def stats(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(q):
        return values[max(0, math.ceil(len(values) * q) - 1)]
    return (sum(values) / len(values), percentile(.5), percentile(.95),
            percentile(.99), values[-1])


def analyze(folder, skip_seconds, foreground_only):
    metadata = {}
    for line in (folder / "session.txt").read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            metadata[key] = value
    if metadata.get("schema") != "1":
        raise ValueError(f"{folder}: unsupported or missing schema")
    data = {key: [] for key in METRICS}
    totals = collections.Counter()
    configurations = set()
    expected_gpu = {}
    expected_batches = {}
    dropped = 0
    problems = 0
    for row in rows(folder / "frames.csv"):
        if float(row["elapsed_ms"]) < skip_seconds * 1000:
            continue
        if foreground_only and row["foreground"] != "1":
            continue
        frame = int(row["frame"])
        if frame in expected_gpu:
            raise ValueError(f"{folder}: duplicate frame {frame}")
        for key in METRICS + tuple(key for key in OPTIONAL_METRICS if key in row):
            value = float(row[key])
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"{folder}: invalid {key} at frame {frame}")
            data.setdefault(key, []).append(value)
        for key in TOTALS:
            totals[key] += int(row[key])
        expected_gpu[frame] = int(row["world_draws"]) + int(row["hud_draws"]) + (int(row["composite_draws"]) > 0)
        expected_batches[frame] = int(row["world_draws"]) + int(row["hud_draws"])
        configurations.add(tuple(row[key] for key in CONFIG))
        dropped = max(dropped, int(row["log_packets_dropped_total"]))
        expected_draws = (int(row["world_draws"]) + int(row["hud_draws"])
                          + sum(int(row[key]) for key in ("bloom_extract_draws", "bloom_blur_draws",
                                "edge_downsample_draws", "edge_blur_draws", "composite_draws")))
        problems += expected_draws != int(row["draw_calls"])
    if not expected_gpu:
        raise ValueError(f"{folder}: no retained frames; reduce --skip-seconds or remove --foreground-only")
    by_frame_gpu = collections.defaultdict(dict)
    stage_times = collections.defaultdict(list)
    for row in rows(folder / "gpu.csv"):
        frame = int(row["frame"])
        if frame not in expected_gpu:
            continue
        key = (int(row["batch"]), row["stage"])
        value = float(row["gpu_ms"])
        if not math.isfinite(value) or value < 0 or key in by_frame_gpu[frame]:
            raise ValueError(f"{folder}: invalid or duplicate GPU sample at frame {frame}")
        by_frame_gpu[frame][key] = value
    complete_gpu = []
    for frame, expected in expected_gpu.items():
        samples = by_frame_gpu.get(frame, {})
        if len(samples) == expected:
            complete_gpu.append(sum(samples.values()))
            grouped = collections.Counter()
            for (_, stage), value in samples.items():
                grouped[stage] += value
            for stage, value in grouped.items():
                stage_times[stage].append(value)
    reasons = collections.Counter()
    batch_counts = collections.Counter()
    fill = []
    for row in rows(folder / "batches.csv"):
        frame = int(row["frame"])
        if frame in expected_gpu:
            reasons[(row["stage"], row["reason"])] += 1
            batch_counts[frame] += 1
            fill.append(float(row["instance_fill_ratio"]))
    batch_gaps = sum(batch_counts[frame] != count for frame, count in expected_batches.items())
    output = [f"성능 로그 분석: {folder.resolve()}",
              f"준비 구간 제외: {skip_seconds:g}초 / 전경 프레임만: {foreground_only}",
              f"분석 프레임: {len(expected_gpu)} / 설정 조합: {len(configurations)}",
              f"선택 표본 FPS 추정: {1000 / stats(data['frame_ms'])[0]:.2f}",
              "표본 누락·필터링 시 위 값은 전체 세션 FPS가 아닙니다.",
              f"기록 누락 패킷: {max(dropped, int(metadata.get('log_packets_dropped', 0)))}",
              f"파일 오류/상한: {metadata.get('log_file_failed', '종료 정보 없음')}/{metadata.get('log_limit_reached', '?')}",
              f"draw 합계 불일치 프레임: {problems} / 배치 행 불일치 프레임: {batch_gaps}",
              f"GPU 측정 완비 프레임: {len(complete_gpu)}/{len(expected_gpu)}",
              "GPU 누락을 0ms로 대체하지 않으며, 완비된 프레임만 합산합니다.", "",
              "지표: 평균 / p50 / p95 / p99 / 최댓값 (원본 열의 ms 또는 bytes 단위)"]
    for key, values in data.items():
        output.append(f"{key}: " + " / ".join(f"{value:.4f}" for value in stats(values)))
    if complete_gpu:
        output.append("gpu_measured_scopes_sum_ms: " + " / ".join(f"{v:.4f}" for v in stats(complete_gpu)))
        for stage, values in sorted(stage_times.items()):
            output.append(f"gpu_{stage}_per_frame_ms: " + " / ".join(f"{v:.4f}" for v in stats(values)))
    output += ["", "구간 합계"]
    output += [f"{key}: {totals[key]}" for key in TOTALS]
    hits = totals["cache_hits"]
    attempts = hits + totals["cache_misses"]
    output.append(f"메모리 조회 적중률: {hits / attempts * 100 if attempts else 0:.2f}% (조회 횟수 가중)")
    output += ["", "배치 종료 사유"]
    output += [f"{stage}/{reason}: {count}" for (stage, reason), count in sorted(reasons.items())]
    if fill:
        output.append(f"인스턴스 배치 평균 채움률: {sum(fill) / len(fill):.4f}")
    output += ["", "해석 주의",
               "- CPU 구간은 중첩되므로 더해서 총 프레임 시간으로 계산하지 않습니다.",
               "- GPU 합계는 측정한 렌더링 구간 합계이며 전체 장치 사용 시간이나 CPU+GPU 총합이 아닙니다.",
               "- 설정이 섞이면 events.csv의 manual_marker/configuration을 기준으로 구간을 분리하세요.",
               "- 짧은 구간 p99, 초기 캐시 생성, 백그라운드 전환 및 로그 누락을 구분하세요."]
    return output, {key: stats(values)[0] for key, values in data.items()}, metadata, configurations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--skip-seconds", type=float, default=5)
    parser.add_argument("--foreground-only", action="store_true")
    args = parser.parse_args()
    if not math.isfinite(args.skip_seconds) or args.skip_seconds < 0:
        parser.error("--skip-seconds must be finite and nonnegative")
    try:
        report, current, metadata, configurations = analyze(args.session, args.skip_seconds, args.foreground_only)
        if args.baseline:
            _, baseline, old_metadata, old_configurations = analyze(args.baseline, args.skip_seconds, args.foreground_only)
            report += ["", f"기준 실행과 비교: {args.baseline.resolve()}", "변화율 = (현재/기준 - 1) × 100. 음수는 해당 비용 감소."]
            comparison_metrics = METRICS + tuple(key for key in OPTIONAL_METRICS
                                                if key in current and key in baseline)
            for key in comparison_metrics:
                change = f"{(current[key] / baseline[key] - 1) * 100:+.2f}%" if baseline[key] else "기준 0: 변화율 없음"
                report.append(f"{key}: {baseline[key]:.4f} -> {current[key]:.4f} ({change})")
            mismatched = [key for key in ("configuration", "gl_vendor", "gl_renderer", "gl_version", "pointer_bits")
                          if metadata.get(key) != old_metadata.get(key)]
            if mismatched or configurations != old_configurations:
                report.append("주의: 실행 환경/설정이 다릅니다. 성능 변화의 원인을 코드 변경으로 단정할 수 없습니다.")
                report.append("환경 차이: " + ", ".join(mismatched))
        output = args.session / "analysis_report.txt"
        output.write_text("\n".join(report) + "\n", encoding="utf-8")
        print(f"분석 리포트: {output.resolve()}")
    except (OSError, ValueError, KeyError, csv.Error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
