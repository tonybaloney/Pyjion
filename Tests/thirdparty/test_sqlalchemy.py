import pytest
try:
    from sqlalchemy import Column, select, Integer, String, create_engine
    from sqlalchemy.orm import declarative_base, Session

    has_lib = True
except ImportError:
    has_lib = False


@pytest.mark.graph
@pytest.mark.skipif(not has_lib, reason="Missing library")
@pytest.mark.external
def test_base_type():
    # declarative base class
    Base = declarative_base()
    engine = create_engine('sqlite://')

    class Post(Base):
        __tablename__ = 'post'
        id = Column(Integer, autoincrement=True, primary_key=True)
        name = Column(String)

    # create session and add objects
    with Session(engine) as session:
        Base.metadata.create_all(engine)

        session.add(Post(name='first post'))
        session.add(Post(name='second post'))
        session.commit()

        sp = select(Post)
        ps = session.execute(select(Post)).scalars().all()
        assert len(ps) == 2