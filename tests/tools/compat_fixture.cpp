extern "C" __declspec(dllexport) int FixtureRequired(const int value)
{
  return value + 1;
}

extern "C" __declspec(dllexport) int FixtureSecond(const int value)
{
  return value + 2;
}
