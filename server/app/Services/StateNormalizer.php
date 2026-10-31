<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\Text;

/** Turns whatever a controller reports into a small, typed state and a list of history readings. */
final class StateNormalizer
{
    public const RUN_STATES = ['unknown', 'off', 'starting', 'on', 'cooling'];

    public function normalize(array $in): array
    {
        $env = null;
        if (is_array($in['env'] ?? null) && isset($in['env']['t'], $in['env']['h'])) {
            $env = [
                't' => round($this->clamped($in['env']['t'], -100, 200, true) ?? 0.0, 1),
                'h' => round($this->clamped($in['env']['h'], 0, 100, true) ?? 0.0),
                'age' => $this->int($in['env']['age'] ?? null),
            ];
        }
        $heaters = [];
        $list = is_array($in['heaters'] ?? null) ? $in['heaters'] : [];
        foreach (array_slice($list, 0, 2) as $h) {
            if (!is_array($h)) {
                continue;
            }
            $heaters[] = [
                'i' => max(0, min(1, (int)(is_numeric($h['i'] ?? null) ? $h['i'] : 0))),  // heater slot: 0 or 1
                'name' => Text::cut(Text::scalar($h['name'] ?? ''), 40),
                'run' => in_array($h['run'] ?? '', self::RUN_STATES, true) ? $h['run'] : 'unknown',
                'manual' => (bool)($h['manual'] ?? false),
                'level' => $this->int($h['level'] ?? null),
                'setpoint' => $this->int($h['setpoint'] ?? null),
                'chamber' => $this->int($h['chamber'] ?? null),
                'volts' => $this->int($h['volts'] ?? null),
                'age' => $this->int($h['age'] ?? null),
                'busy' => isset($h['busy']) ? Text::cut(Text::scalar($h['busy']), 40) : null,
                'err' => isset($h['err']) ? Text::cut(Text::scalar($h['err']), 120) : null,
                'smart' => is_array($h['smart'] ?? null) ? [
                    'on' => true,
                    'target' => $this->clamped($h['smart']['target'] ?? 0, 0, 100) ?? 0,
                    'phase' => Text::cut(Text::scalar($h['smart']['phase'] ?? ''), 20),
                    'level' => $this->int($h['smart']['level'] ?? null),
                    'rate' => isset($h['smart']['rate']) && is_numeric($h['smart']['rate']) ? round((float)$h['smart']['rate'], 1) : null,
                ] : null,
            ];
        }
        return [
            'lang' => preg_match('/^[A-Za-z]{2}$/', (string)($in['lang'] ?? '')) ? strtolower($in['lang']) : null,  // language of the firmware build
            'rssi' => $this->int($in['rssi'] ?? null),
            'uptime' => $this->int($in['uptime'] ?? null),
            'env' => $env,
            'heaters' => $heaters,
        ];
    }

    /** @return array<int, array{0:string,1:int,2:float}> kind, heater, value */
    public function readings(array $state): array
    {
        $rows = [];
        if ($state['env']) {
            $rows[] = ['env_t', 0, (float)$state['env']['t']];
            $rows[] = ['env_h', 0, (float)$state['env']['h']];
        }
        foreach ($state['heaters'] as $h) {
            if ($h['chamber'] !== null) {
                $rows[] = ['chamber', $h['i'], (float)$h['chamber']];
            }
            if ($h['volts'] !== null) {
                $rows[] = ['volts', $h['i'], (float)$h['volts']];
            }
        }
        return $rows;
    }

    /** A number from the controller, limited to what the columns and the charts can take (the database refuses the rest). */
    private function int(mixed $v): ?int
    {
        return $this->clamped($v, -30000, 30000);
    }

    /** @return int|float|null the value inside [lo, hi] (a float when $float), null when it is not a number */
    private function clamped(mixed $v, float $lo, float $hi, bool $float = false): int|float|null
    {
        if (!is_numeric($v)) {
            return null;
        }
        $x = max($lo, min($hi, (float)$v));
        return $float ? $x : (int)$x;
    }
}
