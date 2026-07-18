from types import SimpleNamespace


def create_advanced_workspace(tmp):
    tmp.mkdir(parents=True)
    model_source = """# Generic storage used by the advanced editor matrix.
class Buffer[T, N]:
    values: array[T][N]

    def first(self) -> T:
        return self.values[0]


# Copies a shaped array while preserving its element and extent parameters.
def copy_values[T, N](source: &const[array[T][N]]) -> array[T][N]:
    result: array[T][N]
    for index: usize in range(N):
        result[index] = source[index]
    return result


type FourInts = Buffer[i32, 4]


enum Packet:
    Empty
    Data:
        value: i32


class Leaf:
    value: i32


class Branch:
    leaf: Leaf


class Root:
    branch: Branch


class Overloaded:
    def choose(self, value: i32) -> i32:
        return value

    def choose(self, value: str) -> i32:
        return i32(len(value))
"""
    model = tmp / "model.dd"
    model.write_text(model_source)

    inheritance_source = """class Processor:
    @virtual
    def transform(self, value: i32) -> i32:
        return value


class ScalingProcessor(Processor):
    factor: i32

    @override
    def transform(self, value: i32) -> i32:
        return super.transform(value) * self.factor


class Source:
    @abstract
    def read(self) -> i32:


class ConstantSource(Source):
    value: i32

    @override
    def read(self) -> i32:
        return self.value
"""
    inheritance = tmp / "inheritance.dd"
    inheritance.write_text(inheritance_source)

    facade_source = """from model import Buffer
from model import FourInts
from model import Packet
from model import Root
from model import Branch
from model import Leaf
from model import Overloaded
from model import copy_values
from inheritance import ScalingProcessor
from inheritance import ConstantSource
"""
    facade = tmp / "api.dd"
    facade.write_text(facade_source)

    other_source = """class Buffer[T, N]:
    values: array[T][N]


class ScalingProcessor:
    factor: i32

    def transform(self, value: i32) -> i32:
        return value - self.factor
"""
    other = tmp / "other.dd"
    other.write_text(other_source)

    main_source = """import model as api
import inheritance as hierarchy
import other
from model import Packet


def consume(packet: Packet, values: &const[array[i32][4]]) -> i32:
    total = 0
    for item in values:
        total += item
    match packet:
        case Packet.Empty:
            return total
        case Packet.Data(payload):
            return total + payload


def main() -> i32:
    values: array[i32][4] = [1, 2, 3, 4]
    copied = api.copy_values(values)
    middle = copied[1:3]
    buffer: api.FourInts = api.Buffer[i32, 4](copied)
    processor = hierarchy.ScalingProcessor(2)
    other_processor = other.ScalingProcessor(9)
    source = hierarchy.ConstantSource(5)
    root = api.Root(api.Branch(api.Leaf(11)))
    deep = root.branch.leaf.value
    overloaded = api.Overloaded()
    number_choice = overloaded.choose(3)
    text_choice = overloaded.choose("three")
    transformed = processor.transform(buffer.first())
    ignored = other_processor.transform(20)
    packet = Packet.Data(7)
    return consume(packet, copied) + middle[0] + transformed + source.read() + ignored + deep + number_choice + text_choice
"""
    main = tmp / "main.dd"
    main.write_text(main_source)

    return SimpleNamespace(**locals())
