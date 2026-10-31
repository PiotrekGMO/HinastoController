<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use DateTimeImmutable;
use DateTimeZone;
use Hinasto\Core\Config;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Repositories\ReadingRepository;
use Hinasto\Repositories\RunRepository;
use Hinasto\Repositories\StepRepository;
use Hinasto\Services\FuelModel;

/** The analysis page: everything about one heater for a chosen time range in a single answer. */
final class AnalysisController
{
    public function analysis(Request $request): Response
    {
        $device = (int)$request->query('device', '0');
        $heater = (int)$request->query('heater', '0');
        $keepDays = max(1, (int)Config::get('retention_days', 7));
        $hours = max(1, min($keepDays * 24, (int)$request->query('hours', '24')));
        $now = time();
        $from = $now - $hours * 3600;

        $readings = new ReadingRepository();
        $runs = (new RunRepository())->intervals($device, $heater, $from, $now);
        $steps = (new StepRepository())->intervals($device, $heater, $from, $now);
        $lph = FuelModel::lph();
        $startL = FuelModel::startLitres();

        $whole = $this->summary($runs, $steps, $from, $now, $lph, $startL);
        $whole['temp'] = $readings->stats($device, 'env_t', 0, $hours);
        $whole['chamber_max'] = $readings->stats($device, 'chamber', $heater, $hours)['max'];

        return Response::json([
            'from' => $from,
            'to' => $now,
            'retention_days' => $keepDays,
            'series' => [
                'env_t' => $readings->series($device, 'env_t', 0, $hours),
                'env_h' => $readings->series($device, 'env_h', 0, $hours),
                'chamber' => $readings->series($device, 'chamber', $heater, $hours),
                'volts' => $readings->series($device, 'volts', $heater, $hours),
            ],
            'runs' => $runs,
            'steps' => $steps,
            'summary' => $whole,
            'daily' => $this->daily($runs, $steps, $from, $now, $lph, $startL),
            'lph' => array_values($lph),
        ]);
    }

    /** Work time, starts, time per level and mode, and the fuel estimate for [$from, $to]. */
    private function summary(array $runs, array $steps, int $from, int $to, array $lph, float $startL): array
    {
        $work = 0;
        $starts = 0;
        foreach ($runs as [$s, $e]) {
            $work += max(0, min($e, $to) - max($s, $from));
            $starts += $s >= $from && $s <= $to ? 1 : 0;
        }
        $perLevel = array_fill(1, 6, 0);
        $perMode = ['manual' => 0, 'smart' => 0];
        foreach ($steps as [$s, $e, $mode, $level]) {
            $d = max(0, min($e, $to) - max($s, $from));
            $perLevel[$level] += $d;
            $perMode[$mode] = ($perMode[$mode] ?? 0) + $d;
        }
        $known = array_sum($perLevel);
        $auto = max(0, $work - $known);  // worked without a recorded level: the own auto mode of the heater
        $fuel = 0.0;
        $byLevel = [];
        foreach ($perLevel as $level => $sec) {
            $byLevel[$level] = ['seconds' => $sec, 'litres' => round($sec / 3600 * $lph[$level], 2)];
            $fuel += $sec / 3600 * $lph[$level];
        }
        $startsFuel = $starts * $startL;
        $levelSeconds = 0;
        foreach ($perLevel as $level => $sec) {
            $levelSeconds += $level * $sec;
        }
        return [
            'work_s' => $work,
            'starts' => $starts,
            'by_level' => $byLevel,
            'by_mode' => $perMode,
            'avg_level' => $known > 0 ? round($levelSeconds / $known, 1) : null,
            'auto_s' => $auto,
            'fuel_l' => round($fuel + $startsFuel, 2),
            'fuel_start_l' => round($startsFuel, 2),
            'fuel_auto_min_l' => round($auto / 3600 * $lph[1], 2),
            'fuel_auto_max_l' => round($auto / 3600 * $lph[6], 2),
        ];
    }

    /** One row per local calendar day that touches the range. */
    private function daily(array $runs, array $steps, int $from, int $to, array $lph, float $startL): array
    {
        $tz = new DateTimeZone(date_default_timezone_get());
        $day = (new DateTimeImmutable('@' . $from))->setTimezone($tz)->setTime(0, 0);
        $rows = [];
        while ($day->getTimestamp() <= $to) {
            $next = $day->modify('+1 day');
            $a = max($from, $day->getTimestamp());
            $b = min($to, $next->getTimestamp());
            $sum = $this->summary($runs, $steps, $a, $b, $lph, $startL);
            $rows[] = [
                'day' => $day->format('Y-m-d'),
                'work_s' => $sum['work_s'],
                'fuel_l' => $sum['fuel_l'],
                'starts' => $sum['starts'],
                'auto_s' => $sum['auto_s'],
            ];
            $day = $next;
        }
        return $rows;
    }
}