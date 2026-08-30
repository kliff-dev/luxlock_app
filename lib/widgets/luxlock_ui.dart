import 'dart:ui';

import 'package:flutter/material.dart';

import '../services/luxlock_connection_service.dart';
import '../theme/luxlock_theme.dart';

class LuxLockScaffold extends StatelessWidget {
  final String eyebrow;
  final String title;
  final String subtitle;
  final Widget child;
  final bool showBackButton;
  final VoidCallback? onBackTap;
  final Future<void> Function()? onRefresh;

  const LuxLockScaffold({
    super.key,
    required this.eyebrow,
    required this.title,
    required this.subtitle,
    required this.child,
    this.showBackButton = true,
    this.onBackTap,
    this.onRefresh,
  });

  @override
  Widget build(BuildContext context) {
    final refreshAction =
        onRefresh ?? LuxLockConnectionService.instance.refresh;
    final mediaSize = MediaQuery.sizeOf(context);
    final compactHeader = mediaSize.height < 760 || mediaSize.width < 380;
    final horizontalPadding = compactHeader ? 16.0 : 20.0;
    final topPadding = compactHeader ? 8.0 : 12.0;
    final bottomPadding = compactHeader ? 16.0 : 20.0;
    final titleStyle = Theme.of(context).textTheme.headlineMedium?.copyWith(
      fontSize: compactHeader ? 25 : 29,
      height: compactHeader ? 1.0 : 1.05,
    );
    final subtitleStyle =
        (compactHeader
                ? Theme.of(context).textTheme.bodyMedium
                : Theme.of(context).textTheme.bodyLarge)
            ?.copyWith(
              color: LuxLockPalette.textMuted,
              fontSize: compactHeader ? 12 : 13,
              height: compactHeader ? 1.22 : 1.28,
            );

    return AnimatedBuilder(
      animation: LuxLockConnectionService.instance,
      builder: (context, _) {
        return Scaffold(
          backgroundColor: Colors.transparent,
          body: DecoratedBox(
            decoration: const BoxDecoration(
              gradient: LuxLockPalette.backgroundGradient,
            ),
            child: Stack(
              children: [
                const _GlowOrb(
                  alignment: Alignment(-0.18, -1.08),
                  size: 360,
                  color: LuxLockPalette.gold,
                ),
                const _GlowOrb(
                  alignment: Alignment(1.16, -0.52),
                  size: 300,
                  color: LuxLockPalette.goldDeep,
                ),
                const _GlowOrb(
                  alignment: Alignment(-1.06, 0.96),
                  size: 260,
                  color: LuxLockPalette.mint,
                ),
                const _DiagonalSheen(),
                SafeArea(
                  child: Padding(
                    padding: EdgeInsets.fromLTRB(
                      horizontalPadding,
                      topPadding,
                      horizontalPadding,
                      bottomPadding,
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        _TopBar(
                          showBackButton: showBackButton,
                          onBackTap: onBackTap,
                          compact: compactHeader,
                        ),
                        SizedBox(height: compactHeader ? 12 : 18),
                        Text(
                          eyebrow.toUpperCase(),
                          style: Theme.of(context).textTheme.labelLarge
                              ?.copyWith(
                                color: LuxLockPalette.gold,
                                fontSize: compactHeader ? 12 : 13,
                                letterSpacing: 1.1,
                              ),
                        ),
                        SizedBox(height: compactHeader ? 6 : 8),
                        Text(title, style: titleStyle),
                        SizedBox(height: compactHeader ? 6 : 8),
                        Text(subtitle, style: subtitleStyle),
                        SizedBox(height: compactHeader ? 10 : 12),
                        _ConnectionBanner(compact: compactHeader),
                        SizedBox(height: compactHeader ? 10 : 12),
                        Expanded(
                          child: RefreshIndicator(
                            onRefresh: refreshAction,
                            color: LuxLockPalette.gold,
                            backgroundColor: LuxLockPalette.steel,
                            child: child,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class LuxLockPanel extends StatelessWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? color;

  const LuxLockPanel({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(20),
    this.color,
  });

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(30),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 16, sigmaY: 16),
        child: Container(
          decoration: BoxDecoration(
            color: color ?? LuxLockPalette.panel,
            borderRadius: BorderRadius.circular(30),
            border: Border.all(color: LuxLockPalette.panelBorder),
            boxShadow: const [
              BoxShadow(
                color: Color(0x66000000),
                blurRadius: 32,
                offset: Offset(0, 20),
              ),
              BoxShadow(
                color: Color(0x1AF7D778),
                blurRadius: 28,
                offset: Offset(0, -2),
              ),
            ],
          ),
          padding: padding,
          child: child,
        ),
      ),
    );
  }
}

class LuxLockBadge extends StatelessWidget {
  final String label;
  final Color backgroundColor;
  final Color foregroundColor;

  const LuxLockBadge({
    super.key,
    required this.label,
    required this.backgroundColor,
    required this.foregroundColor,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: backgroundColor,
        borderRadius: BorderRadius.circular(999),
      ),
      child: Text(
        label,
        style: Theme.of(
          context,
        ).textTheme.labelLarge?.copyWith(color: foregroundColor, fontSize: 12),
      ),
    );
  }
}

class LuxLockLogoCard extends StatelessWidget {
  final double height;

  const LuxLockLogoCard({super.key, this.height = 154});

  @override
  Widget build(BuildContext context) {
    return Container(
      height: height,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(30),
        border: Border.all(color: LuxLockPalette.panelBorder),
        boxShadow: const [
          BoxShadow(
            color: Color(0x52000000),
            blurRadius: 34,
            offset: Offset(0, 20),
          ),
          BoxShadow(
            color: Color(0x2AF7D778),
            blurRadius: 44,
            offset: Offset(0, -8),
          ),
        ],
      ),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(30),
        child: DecoratedBox(
          decoration: const BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: [LuxLockPalette.midnight, LuxLockPalette.obsidian],
            ),
          ),
          child: Padding(
            padding: const EdgeInsets.all(8),
            child: Image.asset(
              LuxLockAssets.logo,
              fit: BoxFit.contain,
              alignment: Alignment.center,
            ),
          ),
        ),
      ),
    );
  }
}

class _TopBar extends StatelessWidget {
  final bool showBackButton;
  final VoidCallback? onBackTap;
  final bool compact;

  const _TopBar({
    required this.showBackButton,
    this.onBackTap,
    required this.compact,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        if (showBackButton)
          _TopBarButton(
            icon: Icons.arrow_back_rounded,
            onTap: onBackTap ?? () => Navigator.of(context).pop(),
            compact: compact,
          )
        else
          _TopBarBrand(compact: compact),
        Expanded(
          child: Center(child: _TopBarLogo(compact: compact)),
        ),
        SizedBox(width: compact ? 44 : 48),
      ],
    );
  }
}

class _TopBarBrand extends StatelessWidget {
  final bool compact;

  const _TopBarBrand({required this.compact});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: EdgeInsets.symmetric(
        horizontal: compact ? 12 : 14,
        vertical: compact ? 8 : 10,
      ),
      decoration: BoxDecoration(
        color: const Color(0x16000000),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: const Color(0x44F7D778)),
        boxShadow: const [
          BoxShadow(
            color: Color(0x26000000),
            blurRadius: 18,
            offset: Offset(0, 8),
          ),
        ],
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(10),
            child: Image.asset(
              LuxLockAssets.logo,
              width: compact ? 26 : 30,
              height: compact ? 26 : 30,
              fit: BoxFit.cover,
            ),
          ),
          SizedBox(width: compact ? 8 : 10),
          Text(
            'LuxLock',
            style: Theme.of(context).textTheme.titleMedium?.copyWith(
              fontWeight: FontWeight.w800,
              fontSize: compact ? 15 : null,
            ),
          ),
        ],
      ),
    );
  }
}

class _TopBarButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback? onTap;
  final bool compact;

  const _TopBarButton({required this.icon, this.onTap, this.compact = false});

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(18),
      child: Ink(
        width: compact ? 44 : 48,
        height: compact ? 44 : 48,
        decoration: BoxDecoration(
          color: const Color(0x1AF7D778),
          borderRadius: BorderRadius.circular(18),
          border: Border.all(color: const Color(0x44F7D778)),
          boxShadow: const [
            BoxShadow(
              color: Color(0x1FF7D778),
              blurRadius: 18,
              offset: Offset(0, 6),
            ),
          ],
        ),
        child: Icon(icon, color: LuxLockPalette.textPrimary),
      ),
    );
  }
}

class _TopBarLogo extends StatelessWidget {
  final bool compact;

  const _TopBarLogo({required this.compact});

  @override
  Widget build(BuildContext context) {
    final size = compact ? 58.0 : 62.0;

    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(compact ? 20 : 22),
        border: Border.all(color: const Color(0x55F7D778)),
        boxShadow: const [
          BoxShadow(
            color: Color(0x33000000),
            blurRadius: 18,
            offset: Offset(0, 10),
          ),
          BoxShadow(
            color: Color(0x2AF7D778),
            blurRadius: 20,
            offset: Offset(0, -3),
          ),
        ],
      ),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(compact ? 19 : 21),
        child: Image.asset(LuxLockAssets.logo, fit: BoxFit.cover),
      ),
    );
  }
}

class _ConnectionBanner extends StatelessWidget {
  final bool compact;

  const _ConnectionBanner({required this.compact});

  @override
  Widget build(BuildContext context) {
    final connection = LuxLockConnectionService.instance;
    final isHealthy = connection.canSendCommands;
    final accent = isHealthy ? LuxLockPalette.mint : LuxLockPalette.coral;
    final icon = isHealthy ? Icons.cloud_done_rounded : Icons.cloud_off_rounded;

    return Container(
      width: double.infinity,
      padding: EdgeInsets.symmetric(
        horizontal: compact ? 12 : 14,
        vertical: compact ? 10 : 12,
      ),
      decoration: BoxDecoration(
        color: accent.withValues(alpha: isHealthy ? 0.12 : 0.16),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(
          color: accent.withValues(alpha: isHealthy ? 0.42 : 0.32),
        ),
        boxShadow: [
          if (isHealthy)
            BoxShadow(
              color: LuxLockPalette.mint.withValues(alpha: 0.12),
              blurRadius: 20,
              offset: const Offset(0, 10),
            ),
        ],
      ),
      child: Row(
        children: [
          Icon(icon, size: 18, color: accent),
          SizedBox(width: compact ? 8 : 10),
          Expanded(
            child: Text(
              connection.canSendCommands
                  ? 'Connected to Firebase. Pull down anytime to refresh.'
                  : '${connection.statusLabel}. Commands are disabled until the app reconnects.',
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                fontSize: compact ? 13 : null,
                color: isHealthy
                    ? LuxLockPalette.textPrimary
                    : LuxLockPalette.textPrimary,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _GlowOrb extends StatelessWidget {
  final AlignmentGeometry alignment;
  final double size;
  final Color color;

  const _GlowOrb({
    required this.alignment,
    required this.size,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: alignment,
      child: IgnorePointer(
        child: Container(
          width: size,
          height: size,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            gradient: RadialGradient(
              colors: [
                color.withValues(alpha: 0.25),
                color.withValues(alpha: 0),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _DiagonalSheen extends StatelessWidget {
  const _DiagonalSheen();

  @override
  Widget build(BuildContext context) {
    return Positioned.fill(
      child: IgnorePointer(
        child: CustomPaint(painter: _DiagonalSheenPainter()),
      ),
    );
  }
}

class _DiagonalSheenPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..shader = LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: [
          Colors.white.withValues(alpha: 0.05),
          Colors.white.withValues(alpha: 0),
        ],
      ).createShader(Offset.zero & size);

    final path = Path()
      ..moveTo(size.width * 0.08, 0)
      ..lineTo(size.width * 0.56, 0)
      ..lineTo(size.width * 0.18, size.height)
      ..lineTo(0, size.height)
      ..close();

    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
