<main class="wrap narrow">
  <div class="brand big">
    <img src="<?= $e($base) ?>/assets/logo.svg" alt="" width="48" height="48">
    <div class="brand-text"><strong>Hinasto</strong><span>Controller</span></div>
  </div>

  <section class="card form">
    <h1><?= $t('manual.title') ?></h1>
    <p><?= $th('manual.text') ?></p>
    <ol class="steps">
      <li><?= $th('manual.s1') ?></li>
      <li><?= $th('manual.s2') ?></li>
      <li><?= $th('manual.s3', ['base' => $base]) ?></li>
    </ol>
    <textarea class="codebox" readonly rows="16" onfocus="this.select()"><?= $e($configPhp) ?></textarea>
    <p class="muted small"><?= $th('manual.warn', ['key' => $enrollKey]) ?></p>
    <p class="muted small"><?= $th('manual.perm') ?></p>
  </section>
</main>
