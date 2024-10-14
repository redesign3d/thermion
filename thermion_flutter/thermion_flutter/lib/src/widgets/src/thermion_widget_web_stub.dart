import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:thermion_flutter/thermion_flutter.dart';
import 'package:thermion_flutter_web/thermion_flutter_web_options.dart';

class ThermionWidgetWeb extends StatelessWidget {
  final ThermionFlutterWebOptions? options;
  final ThermionViewer viewer;

  const ThermionWidgetWeb({super.key, required this.options, required this.viewer});

  @override
  Widget build(BuildContext context) {
    throw Exception("STUB");
  }
}
