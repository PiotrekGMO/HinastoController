<header class="topbar">
  <div class="brand">
    <img src="<?= $e($base) ?>/assets/logo.svg" alt="" width="36" height="36">
    <div class="brand-text"><strong>Hinasto</strong><span>Controller</span></div>
  </div>
  <button id="menuBtn" class="menu-btn" aria-label="Menu" aria-expanded="false" aria-controls="topActions" hidden><i></i></button>
  <div class="topbar-actions" id="topActions">
    <button id="openAnalytics" class="btn ghost" hidden><?= $t('top.analytics') ?></button>
    <button id="installApp" class="btn ghost" hidden><?= $t('top.install') ?></button>
    <button id="addDevice" class="btn ghost" hidden><?= $t('top.add') ?></button>
    <button id="firmware" class="btn ghost" hidden><?= $t('top.firmware') ?></button>
    <button id="logout" class="btn ghost" hidden><?= $t('top.logout') ?></button>
  </div>
</header>

<main class="wrap">
  <div id="msg" class="flash" role="alert" hidden></div>

  <section id="login" hidden>
    <form id="loginForm" class="card login">
      <h1><?= $t('login.title') ?></h1>
      <p class="muted"><?= $t('login.subtitle') ?></p>
      <label class="field">
        <span><?= $t('login.password') ?></span>
        <input id="pw" type="password" autocomplete="current-password" required>
      </label>
      <button class="btn primary" type="submit"><?= $t('login.submit') ?></button>
    </form>
  </section>

  <section id="analytics">
    <article class="card ana">
      <div class="ana-head">
        <h1><?= $t('ana.title') ?></h1>
        <label class="field ana-pick"><span><?= $t('ana.heater') ?></span><select id="anaDevice"></select></label>
      </div>
      <div class="ranges" id="anaRanges">
        <button class="chip" data-h="6">6 h</button>
        <button class="chip" data-h="24">24 h</button>
        <button class="chip" data-h="72"><?= $t('ana.d3') ?></button>
        <button class="chip" data-h="168"><?= $t('ana.d7') ?></button>
      </div>
      <p class="muted small" id="anaNote"></p>

      <div class="tiles three">
        <div class="tile"><span><?= $t('ana.work') ?></span><strong data-ana="work"></strong></div>
        <div class="tile"><span><?= $t('ana.starts') ?></span><strong data-ana="starts"></strong></div>
        <div class="tile"><span><?= $t('ana.fuel') ?></span><strong data-ana="fuel"></strong></div>
      </div>
      <div class="tiles three">
        <div class="tile"><span><?= $t('ana.tmin') ?></span><strong data-ana="tmin"></strong></div>
        <div class="tile"><span><?= $t('ana.tavg') ?></span><strong data-ana="tavg"></strong></div>
        <div class="tile"><span><?= $t('ana.tmax') ?></span><strong data-ana="tmax"></strong></div>
      </div>
      <div class="tiles three">
        <div class="tile"><span><?= $t('ana.avg_level') ?></span><strong data-ana="level"></strong></div>
        <div class="tile"><span><?= $t('ana.chamber_max') ?></span><strong data-ana="chamber"></strong></div>
        <div class="tile"><span><?= $t('ana.smart_mode') ?></span><strong data-ana="smart"></strong></div>
      </div>
      <p class="muted small" id="anaFuelNote"></p>

      <h2 class="ana-h"><?= $t('ana.course') ?></h2>
      <div class="ranges" id="anaMetrics">
        <button class="chip" data-m="env_t"><?= $t('ana.m.env_t') ?></button>
        <button class="chip" data-m="env_h"><?= $t('ana.m.env_h') ?></button>
        <button class="chip" data-m="chamber"><?= $t('ana.m.chamber') ?></button>
        <button class="chip" data-m="volts"><?= $t('ana.m.volts') ?></button>
      </div>
      <div class="chart"><div class="chart-box"><svg id="anaChart" role="img" aria-label="<?= $t('ana.chart_aria') ?>"></svg><div class="tip" id="anaTip" hidden></div></div></div>

      <h2 class="ana-h"><?= $t('ana.power') ?></h2>
      <div class="chart"><div class="chart-box"><svg id="anaLevels" role="img" aria-label="<?= $t('ana.power_aria') ?>"></svg><div class="tip" id="anaTip2" hidden></div></div>
        <div class="muted small legend2"><i class="sw manual"></i> <?= $t('ana.legend_manual') ?> <i class="sw smart"></i> <?= $t('ana.legend_smart') ?> <i class="sw band"></i> <?= $t('ana.legend_band') ?></div>
      </div>

      <h2 class="ana-h"><?= $t('ana.per_level') ?></h2>
      <div id="anaLevelBars"></div>

      <h2 class="ana-h"><?= $t('ana.daily') ?></h2>
      <div id="anaDaily"></div>
    </article>
  </section>

  <section id="app" hidden>
    <div id="devices"></div>
    <div id="empty" class="card empty" hidden>
      <img src="<?= $e($base) ?>/assets/logo.svg" alt="" width="56" height="56">
      <h2><?= $t('empty.title') ?></h2>
      <p class="muted"><?= $t('empty.text') ?></p>
    </div>
  </section>
</main>

<div id="updateBar" class="update-bar" role="status" hidden><span><?= $t('update.text') ?></span><button class="btn primary" id="updateNow"><?= $t('update.button') ?></button></div>
<div id="modal" class="overlay" hidden></div>
<div id="info" class="overlay" hidden></div>

<!-- Templates: all markup lives here, app.js only clones and fills them. -->

<template id="tpl-device">
  <article class="card device">
    <div class="head">
      <div class="title">
        <h2 data-bind="name"></h2>
        <div class="muted small" data-bind="meta"></div>
      </div>
      <span class="status" data-bind="online"><i></i><b></b></span>
    </div>
    <div class="muted small" data-bind="seen"></div>

    <div class="tiles" data-env hidden>
      <div class="tile"><span><?= $t('dev.env_t') ?></span><strong data-bind="envT"></strong></div>
      <div class="tile"><span><?= $t('dev.env_h') ?></span><strong data-bind="envH"></strong></div>
    </div>
    <div class="chart" data-spark hidden>
      <div class="chart-head">
        <div class="muted small" data-bind="sparkNote"></div>
        <div class="ranges">
          <button class="chip" data-r="1">1 h</button>
          <button class="chip" data-r="3">3 h</button>
          <button class="chip" data-r="6">6 h</button>
          <button class="chip" data-r="12">12 h</button>
          <button class="chip" data-r="24">24 h</button>
        </div>
      </div>
      <div class="chart-box">
        <svg role="img" aria-label="<?= $t('dev.chart_aria') ?>"></svg>
        <div class="tip" data-tip hidden></div>
      </div>
      <div class="muted small legend" data-legend hidden><i class="sw"></i> <?= $t('dev.legend') ?></div>
    </div>
    <div class="tiles three" data-runtiles hidden>
      <div class="tile"><span><?= $t('dev.run_today') ?></span><strong data-bind="runDay"></strong></div>
      <div class="tile"><span><?= $t('dev.run_24h') ?></span><strong data-bind="run24"></strong></div>
      <div class="tile"><span data-bind="runTotalLabel"><?= $t('dev.run_total') ?></span><strong data-bind="runTotal"></strong></div>
    </div>

    <div data-heaters></div>
    <p class="muted small" data-noheaters><?= $t('dev.no_heaters') ?></p>

    <div class="cmds small" data-cmds></div>

    <div class="foot">
      <button class="btn ghost" data-a="up" aria-label="<?= $t('dev.up_aria') ?>" title="<?= $t('dev.up') ?>">↑</button>
      <button class="btn ghost" data-a="down" aria-label="<?= $t('dev.down_aria') ?>" title="<?= $t('dev.down') ?>">↓</button>
      <button class="btn ghost" data-a="console"><?= $t('dev.console') ?></button>
      <div class="more">
        <button class="btn ghost more-btn" data-more aria-label="<?= $t('dev.more') ?>" aria-expanded="false">⋯</button>
        <div class="more-list">
          <button class="btn ghost" data-a="rename"><?= $t('dev.rename') ?></button>
          <button class="btn ghost danger" data-a="delete"><?= $t('dev.delete') ?></button>
        </div>
      </div>
    </div>
    <pre class="console" data-console hidden></pre>
  </article>
</template>

<template id="tpl-heater">
  <section class="heater">
    <div class="head">
      <h3 data-bind="title"></h3>
      <span class="status" data-bind="run"><i></i><b></b></span>
    </div>
    <div class="tiles">
      <div class="tile"><span><?= $t('heater.chamber') ?></span><strong data-bind="chamber"></strong></div>
      <div class="tile"><span><?= $t('heater.volts') ?></span><strong data-bind="volts"></strong></div>
    </div>
    <p class="error small" data-bind="err" hidden></p>

    <div class="actions">
      <button class="btn primary" data-a="on"><?= $t('heater.on') ?></button>
      <button class="btn" data-a="off"><?= $t('heater.off') ?></button>
    </div>

    <div class="segment" data-modebar>
      <button class="btn" data-a="mode" data-v="1"><?= $t('heater.manual') ?></button>
      <button class="btn" data-a="mode" data-v="0"><?= $t('heater.auto') ?></button>
      <button class="btn" data-a="mode" data-v="2"><?= $t('heater.smart') ?></button>
    </div>

    <div class="panel muted small" data-idle><?= $t('heater.idle') ?></div>

    <div class="panel" data-manual hidden>
      <div class="panel-label"><?= $t('heater.level') ?></div>
      <div class="big" data-bind="level"></div>
      <div class="levels">
        <button class="btn" data-a="level" data-v="1">H1</button>
        <button class="btn" data-a="level" data-v="2">H2</button>
        <button class="btn" data-a="level" data-v="3">H3</button>
        <button class="btn" data-a="level" data-v="4">H4</button>
        <button class="btn" data-a="level" data-v="5">H5</button>
        <button class="btn" data-a="level" data-v="6">H6</button>
      </div>
    </div>

    <div class="panel" data-auto hidden>
      <div class="panel-label" data-bind="autolabel"><?= $t('heater.setpoint') ?></div>
      <div class="big" data-bind="setpoint"></div>
      <p class="muted small smartinfo" data-smartinfo></p>
      <div class="stepper">
        <button class="btn" data-a="bump" data-v="-1" aria-label="<?= $t('heater.less') ?>">−</button>
        <label><input class="sp" inputmode="numeric" aria-label="<?= $t('heater.setpoint') ?>"><span>°C</span></label>
        <button class="btn" data-a="bump" data-v="1" aria-label="<?= $t('heater.more') ?>">+</button>
      </div>
      <button class="btn primary wide" data-a="temp" data-tempbtn><?= $t('heater.set_temp') ?></button>
      <button class="btn wide" data-a="smartoff" data-smartoff hidden><?= $t('heater.smart_off') ?></button>
    </div>
  </section>
</template>

<template id="tpl-firmware">
  <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="fwTitle">
    <h2 id="fwTitle"><?= $t('fw.title') ?></h2>
    <p class="muted small"><?= $t('fw.intro') ?></p>
    <dl>
      <dt><?= $t('fw.firmware') ?></dt><dd data-bind="latest"></dd>
    </dl>
    <ul class="fwlist" data-list></ul>
    <label class="field">
      <span><?= $th('fw.file') ?></span>
      <input type="file" id="fwFile" accept=".bin">
    </label>
    <p class="small" id="fwMsg" hidden></p>
    <div class="buttons">
      <button class="btn primary" data-a="upload"><?= $t('fw.upload') ?></button>
      <button class="btn" data-a="close"><?= $t('close') ?></button>
    </div>
  </div>
</template>

<template id="tpl-ios-install">
  <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="iosTitle">
    <h2 id="iosTitle"><?= $t('ios.title') ?></h2>
    <ol class="steps muted">
      <li><?= $th('ios.s1') ?></li>
      <li><?= $th('ios.s2') ?></li>
      <li><?= $th('ios.s3') ?></li>
    </ol>
    <div class="buttons"><button class="btn primary" data-a="close"><?= $t('close') ?></button></div>
  </div>
</template>

<template id="tpl-enroll">
  <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="enrollTitle">
    <h2 id="enrollTitle"><?= $t('enroll.title') ?></h2>
    <ol class="steps muted">
      <li><?= $th('enroll.s1') ?></li>
      <li><?= $th('enroll.s2') ?></li>
      <li><?= $th('enroll.s3') ?></li>
      <li><?= $th('enroll.s4') ?></li>
    </ol>
    <dl>
      <dt><?= $t('enroll.url') ?></dt><dd><code data-bind="url"></code></dd>
      <dt><?= $t('enroll.key') ?></dt><dd><code data-bind="key"></code></dd>
    </dl>
    <div data-rejected hidden>
      <h3 class="panel-label"><?= $t('enroll.rejected') ?></h3>
      <ul class="fwlist" data-rejected-list></ul>
    </div>
    <div class="buttons">
      <button class="btn" data-a="copy"><?= $t('enroll.copy') ?></button>
      <button class="btn primary" data-a="close"><?= $t('close') ?></button>
    </div>
  </div>
</template>

<template id="tpl-modal">
  <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="dlgTitle">
    <h2 id="dlgTitle"><?= $t('join.title') ?></h2>
    <dl>
      <dt><?= $t('join.id') ?></dt><dd data-bind="device_id"></dd>
      <dt><?= $t('join.ip') ?></dt><dd data-bind="ip"></dd>
      <dt><?= $t('join.fw') ?></dt><dd data-bind="fw"></dd>
      <dt><?= $t('join.first') ?></dt><dd data-bind="first_seen"></dd>
    </dl>
    <label class="field">
      <span><?= $t('join.name') ?></span>
      <input id="newName" maxlength="80" placeholder="<?= $t('join.name_ph') ?>">
    </label>
    <p class="muted small" data-bind="more" hidden></p>
    <div class="buttons">
      <button class="btn primary" data-a="approve"><?= $t('join.add') ?></button>
      <button class="btn" data-a="reject"><?= $t('join.reject') ?></button>
    </div>
  </div>
</template>
<script src="<?= $e($base) ?>/assets/analytics.js?v=<?= $e($assetVersion) ?>"></script>
