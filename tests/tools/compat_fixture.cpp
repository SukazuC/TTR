extern "C" __declspec(dllexport) int FixtureRequired(const int value)
{
  return value + 1;
}

extern "C" __declspec(dllexport) int FixtureSecond(const int value)
{
  return value + 2;
}

struct TtrFixtureInterface
{
  virtual int InterfaceMethod()
  {
    return 3;
  }
};
struct TtrFixturePadding
{
  virtual int PaddingMethod()
  {
    return 4;
  }
};
struct TtrFixtureDerived : TtrFixturePadding, TtrFixtureInterface
{
  int value{};
};
struct TtrFixtureLeft : TtrFixtureInterface
{
};
struct TtrFixtureRight : TtrFixtureInterface
{
};
struct TtrFixtureAmbiguous : TtrFixtureLeft, TtrFixtureRight
{
};

extern "C" __declspec(dllexport) int FixtureUseDerived(TtrFixtureDerived* value)
{
  return value ? value->InterfaceMethod() + value->value : 0;
}

extern "C" __declspec(dllexport) int FixtureUseAmbiguous(TtrFixtureAmbiguous* value)
{
  return value ? value->TtrFixtureLeft::InterfaceMethod() : 0;
}
