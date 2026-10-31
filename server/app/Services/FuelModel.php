<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\Config;

/**
 * Fuel use of the common Chinese diesel air heaters sold as "Webasto 8 kW": about 0.16 l/h at the lowest and 0.82 l/h
 * at the highest of the six levels (the fuel pump is clocked, the dose is proportional), plus a little fuel for every
 * start (priming the pump and the glow plug phase). An estimate, good to some 15 to 20 percent: the real value depends
 * on the pump, the supply voltage and the temperature of the fuel. Calibrate with `fuel_lph` and `fuel_start_l` in
 * storage/config.php when a measured value is known.
 */
final class FuelModel
{
    private const LPH = [1 => 0.16, 2 => 0.29, 3 => 0.43, 4 => 0.56, 5 => 0.69, 6 => 0.82];
    private const START_L = 0.03;

    /** @return array<int, float> litres per hour for level 1..6 */
    public static function lph(): array
    {
        $own = Config::get('fuel_lph');
        if (is_array($own) && count($own) === 6) {
            return array_combine(range(1, 6), array_map('floatval', array_values($own)));
        }
        return self::LPH;
    }

    public static function startLitres(): float
    {
        return (float)Config::get('fuel_start_l', self::START_L);
    }
}